#include "StreamingManager.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include "core/Constants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <QDebug>
#include <QFileInfo>

// --- PlaybackStream ---

bool PlaybackStream::open(const QString& filePath, int64_t startFrame, int64_t endFrame_) {
    SF_INFO streamInfo;
    std::memset(&streamInfo, 0, sizeof(streamInfo));

    SNDFILE* f = sf_open(filePath.toUtf8().constData(), SFM_READ, &streamInfo);
    if (!f) {
        qWarning() << "Failed to open stream file:" << filePath << sf_strerror(nullptr);
        return false;
    }

    sf_seek(f, startFrame, SEEK_SET);
    file = f;
    info = streamInfo;
    endFrame = endFrame_;
    buffer.reset();
    readerFinished = false;
    finished = false;
    return true;
}

void PlaybackStream::close() {
    if (file) {
        sf_close(file);
        file = nullptr;
    }
    finished = true;
}

void PlaybackStream::resetPosition(int64_t startFrame) {
    if (!file) return;
    sf_seek(file, startFrame, SEEK_SET);
    buffer.reset();
    readerFinished = false;
    finished = false;
}

// --- StreamingManager ---

namespace {
// How often the reader thread wakes up to top up the stream ring buffers.
constexpr int kReaderPollMs = 5;

// The file position for an event's stream at playback position `playPos`,
// clamped so the stream never seeks before the event's source offset.
int64_t streamLocalPos(int64_t eventOffsetSample, int64_t eventStartSample,
                       double rate, int64_t playPos) {
    int64_t localPos = static_cast<int64_t>(
        std::llround(eventOffsetSample + (playPos - eventStartSample) * rate));
    return std::max(localPos, eventOffsetSample);
}
}

StreamingManager::StreamingManager() = default;

void StreamingManager::start(Project* project, int64_t playPosition) {
    if (!project) return;

    if (m_readerThread.joinable()) {
        m_readerRunning = false;
        m_readerCond.notify_one();
        m_readerThread.join();
    }

    createStreams(project, playPosition);

    m_readerRunning = true;
    try {
        m_readerThread = std::thread(&StreamingManager::readerThreadFunc, this);
    } catch (const std::system_error& e) {
        qWarning() << "Failed to start reader thread:" << e.what();
        m_readerRunning = false;
    }
}

void StreamingManager::stop() {
    if (m_readerThread.joinable()) {
        m_readerRunning = false;
        m_readerCond.notify_one();
        m_readerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        for (auto& stream : m_playbackStreams)
            stream.close();
        m_playbackStreams.clear();
    }
}

void StreamingManager::createStreams(Project* project, int64_t playPosition) {
    if (!project) return;

    std::shared_lock projectLock(project->mutex());
    std::lock_guard<std::mutex> streamLock(m_streamMutex);

    m_playbackStreams.clear();

    for (const auto& track : project->tracks()) {
        for (const auto& event : track.events()) {
            auto activeClip = event.activeClip();
            if (!activeClip || !activeClip->isValid() || !activeClip->isStreaming())
                continue;

            int64_t eventEnd = event.startSample() + event.durationSample();
            if (playPosition >= eventEnd)
                continue;

            int64_t sourceFrames = event.sourceFrames() > 0
                ? event.sourceFrames() : event.durationSample();
            double rate = sourceFrames / static_cast<double>(event.durationSample());
            if (rate <= 0.0) rate = 1.0;

            int64_t localPos = streamLocalPos(event.offsetSample(),
                                               event.startSample(), rate, playPosition);

            PlaybackStream stream;
            stream.clip = activeClip.get();
            stream.eventStartSample = event.startSample();
            stream.eventOffsetSample = event.offsetSample();
            stream.eventDurationSample = event.durationSample();
            stream.eventSourceFrames = sourceFrames;
            stream.eventRate = rate;
            int64_t endFrame = std::min<int64_t>(event.offsetSample() + sourceFrames,
                                                  activeClip->frameCount());

            if (stream.open(activeClip->filePath(), localPos, endFrame)) {
                m_playbackStreams.push_back(std::move(stream));
            }
        }
    }
}

bool StreamingManager::readEvent(const AudioClip* clip, int64_t eventStartSample,
                                  int64_t eventDurationSample,
                                  float* scratch, size_t maxFrames, int ch,
                                  size_t& outFramesRead) {
    std::unique_lock<std::mutex> lock(m_streamMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        outFramesRead = 0;
        return false;
    }

    PlaybackStream* stream = nullptr;
    for (auto& s : m_playbackStreams) {
        if (s.clip == clip &&
            s.eventStartSample == eventStartSample &&
            s.eventDurationSample == eventDurationSample) {
            stream = &s;
            break;
        }
    }

    if (!stream || stream->finished) {
        outFramesRead = 0;
        return false;
    }

    size_t samplesNeeded = maxFrames * ch;
    size_t samplesRead = stream->buffer.read(scratch, samplesNeeded);
    outFramesRead = samplesRead / ch;

    if (stream->readerFinished && stream->buffer.used() == 0)
        stream->finished = true;

    return outFramesRead > 0;
}

void StreamingManager::signalReset(int64_t newPos) {
    m_needStreamReset.store(true, std::memory_order_release);
    m_resetStreamPos.store(newPos, std::memory_order_release);
    m_readerCond.notify_one();
}

void StreamingManager::closeAll() {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    for (auto& stream : m_playbackStreams)
        stream.close();
    m_playbackStreams.clear();
}

void StreamingManager::readerThreadFunc() {
    std::vector<float> tmp(vvvdaw::ReaderBufferSize);

    auto fillAll = [&] {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        for (auto& stream : m_playbackStreams) {
            if (stream.finished || stream.readerFinished || !stream.file) continue;

            size_t availSamples = stream.buffer.capacity() - stream.buffer.used() - 1;
            if (availSamples == 0) continue;

            size_t ch = stream.info.channels;
            size_t maxFrames = std::min<size_t>(availSamples / ch, tmp.size() / ch);

            sf_count_t currentPos = sf_seek(stream.file, 0, SEEK_CUR);
            sf_count_t framesRemaining = stream.endFrame - currentPos;
            if (framesRemaining <= 0) {
                stream.readerFinished = true;
                continue;
            }
            maxFrames = std::min<size_t>(maxFrames, static_cast<size_t>(framesRemaining));

            sf_count_t framesRead = sf_readf_float(stream.file, tmp.data(), maxFrames);
            if (framesRead > 0) {
                stream.buffer.write(tmp.data(), static_cast<size_t>(framesRead) * ch);
            }
            if (framesRead < maxFrames || framesRemaining <= framesRead) {
                stream.readerFinished = true;
            }
        }
    };

    while (m_readerRunning) {
        if (m_needStreamReset.load(std::memory_order_acquire)) {
            int64_t resetPos = m_resetStreamPos.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lock(m_streamMutex);
                for (auto& stream : m_playbackStreams) {
                    stream.resetPosition(streamLocalPos(stream.eventOffsetSample,
                                                           stream.eventStartSample,
                                                           stream.eventRate, resetPos));
                }
            }
            m_needStreamReset.store(false, std::memory_order_release);
        }

        fillAll();

        std::unique_lock<std::mutex> lock(m_readerMutex);
        m_readerCond.wait_for(lock, std::chrono::milliseconds(kReaderPollMs),
                              [this] { return !m_readerRunning; });
    }

    fillAll();
}
