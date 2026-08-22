#include "RecordingManager.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "core/Constants.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QString>

RecordingManager::RecordingManager() = default;

namespace {
// How often the writer thread wakes up to flush buffered input to disk.
constexpr int kWriterPollMs = 30;
}

void RecordingManager::setScratchSize(size_t frames) {
    m_scratch.resize(frames);
}

bool RecordingManager::copyRecordPeaks(int trackIndex, std::vector<AudioClip::Peak>& out,
                                       int64_t& outRecordedFrames) const {
    std::unique_lock tracksLock(m_recordingTracksMutex);
    auto it = m_recordingTracks.find(trackIndex);
    if (it == m_recordingTracks.end() || !it->second.peaks)
        return false;
    out = it->second.peaks->snapshot();
    outRecordedFrames = it->second.peaks->recordedFrames();
    return true;
}

void RecordingManager::start(Project* project, int sampleRate, int64_t playPosition) {
    if (!project) return;

    if (m_writerThread.joinable()) {
        m_writerRunning = false;
        m_writerCond.notify_one();
        m_writerThread.join();
    }

    std::shared_lock projectLock(project->mutex());

    m_recordingActive = true;
    m_sampleRate = sampleRate;

    if (project->hasRecordRegion()) {
        m_recordStartSample = project->recordRegionStart();
    } else {
        m_recordStartSample = playPosition;
    }

    m_regionRecordingActive.store(!project->hasRecordRegion(), std::memory_order_release);

    QString audioDir = project->audioDirectory();
    QDir().mkpath(audioDir);

    {
        std::unique_lock tracksLock(m_recordingTracksMutex);
        for (size_t i = 0; i < project->tracks().size(); ++i) {
            auto& track = project->tracks()[i];
            if (!track.isRecordArmed()) continue;

            QString filePath = audioDir + QString("/track_%1_%2.wav")
                .arg(static_cast<int>(i))
                .arg(QDateTime::currentMSecsSinceEpoch());

            SF_INFO info;
            std::memset(&info, 0, sizeof(info));
            info.samplerate = sampleRate;
            info.channels = 2;
            info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

            SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
            if (!file) {
                qWarning() << "Failed to open recording file:" << filePath << sf_strerror(nullptr);
                continue;
            }

            RecordingTrack rt;
            rt.buffer = std::make_unique<RingBuffer>(static_cast<size_t>(sampleRate) * vvvdaw::RecordBufferSeconds);
            rt.filePath = filePath.toStdString();
            rt.info = info;
            rt.file = file;
            rt.peaks = std::make_unique<RecordingPeaks>();

            qDebug() << "Recording to:" << filePath;
            m_recordingTracks.emplace(static_cast<int>(i), std::move(rt));
        }
    }

    m_writerRunning = true;
    try {
        m_writerThread = std::thread(&RecordingManager::writerThreadFunc, this);
    } catch (const std::system_error& e) {
        qWarning() << "Failed to start writer thread:" << e.what();
        m_writerRunning = false;
        std::unique_lock tracksLock(m_recordingTracksMutex);
        for (auto& [idx, rt] : m_recordingTracks)
            if (rt.file) {
                sf_close(rt.file);
                rt.file = nullptr;
            }
        m_recordingTracks.clear();
    }
}

void RecordingManager::stop(Project* project) {
    m_recordingActive = false;
    m_regionRecordingActive.store(false, std::memory_order_release);

    if (m_writerThread.joinable()) {
        m_writerRunning = false;
        m_writerCond.notify_one();
        m_writerThread.join();
    }

    // The final drain ran on the writer thread; push any trailing partial
    // peak window so the live preview covers the whole capture.
    {
        std::unique_lock tracksLock(m_recordingTracksMutex);
        for (auto& [idx, rt] : m_recordingTracks)
            if (rt.peaks)
                rt.peaks->flush();
    }

    if (!project) {
        {
            std::unique_lock lock(m_recordingTracksMutex);
            m_recordingTracks.clear();
        }
        return;
    }

    std::unique_lock projectLock(project->mutex());
    std::unique_lock tracksLock(m_recordingTracksMutex);
    for (auto& [trackIdx, rt] : m_recordingTracks) {
        if (rt.file) {
            sf_close(rt.file);
            rt.file = nullptr;
        }

        if (trackIdx < 0 || trackIdx >= static_cast<int>(project->tracks().size()))
            continue;

        auto clip = std::make_shared<AudioClip>(QString::fromStdString(rt.filePath));
        if (!clip->isValid()) {
            qWarning() << "Failed to load recorded file:" << QString::fromStdString(rt.filePath);
            continue;
        }

        bool addedAsTake = false;
        auto& track = project->tracks()[trackIdx];

        addedAsTake = processLoopRecordRegion(*clip, rt, track, project, m_recordStartSample);

        if (!addedAsTake) {
            if (project->hasLoop()) {
                int64_t loopStart = project->loopStart();
                int64_t loopEnd = project->loopEnd();
                qDebug() << "RR_SPLIT: fallback hasLoop loopStart=" << loopStart << "loopEnd=" << loopEnd;
                for (auto& ev : track.events()) {
                    if (ev.startSample() >= loopStart && ev.startSample() < loopEnd) {
                        ev.addTake(clip);
                        addedAsTake = true;
                        qDebug() << "  added whole clip as take to existing event at" << ev.startSample();
                        break;
                    }
                }
            }

            if (!addedAsTake) {
                qDebug() << "RR_SPLIT: creating new event at" << m_recordStartSample
                         << "duration=" << clip->frameCount();
                AudioEvent event;
                event.setClip(clip);
                event.setStartSample(m_recordStartSample);
                event.setOffsetSample(0);
                event.setDurationSample(clip->frameCount());
                event.setSourceFrames(clip->frameCount());
                track.addEvent(event);
            }
        }
    }

    m_recordingTracks.clear();
}

bool RecordingManager::processLoopRecordRegion(AudioClip& clip, const RecordingTrack& rt, Track& track,
                                                Project* proj, int64_t recordStartSample) {
    if (!proj || !proj->hasLoop() || !proj->hasRecordRegion())
        return false;

    int64_t regionLen = proj->recordRegionEnd() - proj->recordRegionStart();
    if (regionLen <= 0)
        return false;

    size_t regionFrames = static_cast<size_t>(regionLen);
    size_t totalFrames = clip.frameCount();
    int ch = clip.channels();
    int sr = clip.sampleRate();

    const float* src = clip.data();
    std::vector<float> ownedData;
    if (!src || clip.isStreaming()) {
        SF_INFO info;
        std::memset(&info, 0, sizeof(info));
        SNDFILE* f = sf_open(rt.filePath.c_str(), SFM_READ, &info);
        if (f) {
            ownedData.resize(static_cast<size_t>(info.frames) * info.channels);
            sf_readf_float(f, ownedData.data(), info.frames);
            sf_close(f);
            src = ownedData.data();
            ch = info.channels;
            sr = info.samplerate;
            totalFrames = info.frames;
        }
    }

    if (totalFrames <= 0 || !src) {
        qDebug() << "RR_SPLIT: skipped! totalFrames=" << totalFrames << "src=" << (void*)src;
        return false;
    }

    AudioEvent* targetEvent = nullptr;
    for (auto& ev : track.events()) {
        if (ev.startSample() == recordStartSample) {
            targetEvent = &ev;
            break;
        }
    }
    if (!targetEvent) {
        AudioEvent newEvent;
        newEvent.setStartSample(recordStartSample);
        newEvent.setOffsetSample(0);
        newEvent.setDurationSample(regionLen);
        newEvent.setSourceFrames(regionLen);
        track.addEvent(newEvent);
        targetEvent = &track.events().back();
    }

    size_t offset = 0;
    int takeNum = 0;
    while (offset < totalFrames) {
        size_t takeFrames = std::min(regionFrames, totalFrames - offset);
        std::vector<float> takeSamples(takeFrames * ch);
        std::copy(src + offset * ch, src + (offset + takeFrames) * ch,
                  takeSamples.begin());

        auto takeClip = std::make_shared<AudioClip>(
            std::move(takeSamples), sr, ch);
        QString takeDir = proj->audioDirectory();
        QDir().mkpath(takeDir);
        QString takePath = takeDir + QString("/take_%1_pass_%2.wav")
            .arg(takeNum).arg(QDateTime::currentMSecsSinceEpoch());
        takeClip->saveToFile(takePath);
        takeClip->setFilePath(takePath);
        targetEvent->addTake(takeClip);
        qDebug() << "  take" << takeNum << "offset=" << offset
                 << "frames=" << takeClip->frameCount();
        offset += takeFrames;
        ++takeNum;
    }

    qDebug() << "RR_SPLIT: done, takes=" << targetEvent->takes().size()
             << "durationSample=" << targetEvent->durationSample()
             << "clip.frames=" << (targetEvent->activeClip() ? targetEvent->activeClip()->frameCount() : 0);
    return true;
}

void RecordingManager::processCapture(const float* input, unsigned long frameCount, int inCh) {
    std::unique_lock lock(m_recordingTracksMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    for (auto& [trackIdx, rt] : m_recordingTracks) {
        if (!rt.buffer) continue;
        if (inCh == 1) {
            for (unsigned long f = 0; f < frameCount; ++f) {
                float s = input[f];
                m_scratch[f * 2] = s;
                m_scratch[f * 2 + 1] = s;
            }
            rt.buffer->write(m_scratch.data(), frameCount * 2);
        } else {
            rt.buffer->write(input, frameCount * 2);
        }
    }
}

void RecordingManager::notifyWriter() {
    m_writerCond.notify_one();
}

void RecordingManager::writerThreadFunc() {
    std::vector<float> tmp(vvvdaw::WriterBufferSize);

    auto drain = [&] {
        for (auto& [trackIdx, rt] : m_recordingTracks) {
            if (!rt.file) continue;
            if (!rt.buffer) continue;
            size_t avail = rt.buffer->used();
            while (avail > 0) {
                size_t toRead = std::min(avail, tmp.size());
                size_t readCount = rt.buffer->read(tmp.data(), toRead);
                if (readCount == 0) break;
                int ch = rt.info.channels > 0 ? rt.info.channels : 2;
                if (rt.peaks)
                    rt.peaks->addSamples(tmp.data(), readCount / static_cast<size_t>(ch), ch);
                sf_writef_float(rt.file, tmp.data(), readCount / 2);
                avail -= readCount;
            }
        }
    };

    while (m_writerRunning) {
        drain();
        std::unique_lock<std::mutex> lock(m_writerMutex);
        m_writerCond.wait_for(lock, std::chrono::milliseconds(kWriterPollMs),
                              [this] { return !m_writerRunning; });
    }

    drain();
}
