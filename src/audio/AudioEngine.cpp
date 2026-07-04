#include "AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

// --- RecordingBuffer ---

RecordingBuffer::RecordingBuffer(size_t capacity)
    : data(capacity, 0.0f)
{
}

size_t RecordingBuffer::write(const float* samples, size_t count) {
    size_t wp = writePos.load(std::memory_order_relaxed);
    size_t rp = readPos.load(std::memory_order_acquire);
    size_t used = (wp >= rp) ? (wp - rp) : (capacity() - rp + wp);
    size_t freeSpace = capacity() - used - 1;
    size_t toWrite = std::min(count, freeSpace);

    for (size_t i = 0; i < toWrite; ++i)
        data[(wp + i) % capacity()] = samples[i];

    writePos.store((wp + toWrite) % capacity(), std::memory_order_release);
    return toWrite;
}

size_t RecordingBuffer::read(float* dest, size_t maxCount) {
    size_t rp = readPos.load(std::memory_order_relaxed);
    size_t wp = writePos.load(std::memory_order_acquire);

    size_t avail;
    if (wp >= rp)
        avail = wp - rp;
    else
        avail = capacity() - rp + wp;

    size_t toRead = std::min(avail, maxCount);
    if (toRead == 0) return 0;

    if (rp + toRead <= capacity()) {
        std::memcpy(dest, data.data() + rp, toRead * sizeof(float));
    } else {
        size_t firstPart = capacity() - rp;
        std::memcpy(dest, data.data() + rp, firstPart * sizeof(float));
        std::memcpy(dest + firstPart, data.data(), (toRead - firstPart) * sizeof(float));
    }

    readPos.store((rp + toRead) % capacity(), std::memory_order_release);
    return toRead;
}

size_t RecordingBuffer::available() const {
    size_t rp = readPos.load(std::memory_order_acquire);
    size_t wp = writePos.load(std::memory_order_acquire);
    if (wp >= rp) return wp - rp;
    return capacity() - rp + wp;
}

// --- PlaybackBuffer ---

PlaybackBuffer::PlaybackBuffer(size_t capacity)
    : data(capacity, 0.0f)
{
}

PlaybackBuffer::PlaybackBuffer(PlaybackBuffer&& other) noexcept
    : data(std::move(other.data))
    , writePos(other.writePos.load(std::memory_order_relaxed))
    , readPos(other.readPos.load(std::memory_order_relaxed))
{
    other.writePos.store(0, std::memory_order_relaxed);
    other.readPos.store(0, std::memory_order_relaxed);
}

PlaybackBuffer& PlaybackBuffer::operator=(PlaybackBuffer&& other) noexcept {
    if (this != &other) {
        data = std::move(other.data);
        writePos.store(other.writePos.load(std::memory_order_relaxed), std::memory_order_relaxed);
        readPos.store(other.readPos.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.writePos.store(0, std::memory_order_relaxed);
        other.readPos.store(0, std::memory_order_relaxed);
    }
    return *this;
}

size_t PlaybackBuffer::write(const float* samples, size_t count) {
    size_t wp = writePos.load(std::memory_order_relaxed);
    size_t rp = readPos.load(std::memory_order_acquire);
    size_t used = (wp >= rp) ? (wp - rp) : (capacity() - rp + wp);
    size_t freeSpace = capacity() - used - 1;
    size_t toWrite = std::min(count, freeSpace);

    for (size_t i = 0; i < toWrite; ++i)
        data[(wp + i) % capacity()] = samples[i];

    writePos.store((wp + toWrite) % capacity(), std::memory_order_release);
    return toWrite;
}

size_t PlaybackBuffer::read(float* dest, size_t maxCount) {
    size_t rp = readPos.load(std::memory_order_relaxed);
    size_t wp = writePos.load(std::memory_order_acquire);

    size_t avail;
    if (wp >= rp)
        avail = wp - rp;
    else
        avail = capacity() - rp + wp;

    size_t toRead = std::min(avail, maxCount);
    if (toRead == 0) return 0;

    if (rp + toRead <= capacity()) {
        std::memcpy(dest, data.data() + rp, toRead * sizeof(float));
    } else {
        size_t firstPart = capacity() - rp;
        std::memcpy(dest, data.data() + rp, firstPart * sizeof(float));
        std::memcpy(dest + firstPart, data.data(), (toRead - firstPart) * sizeof(float));
    }

    readPos.store((rp + toRead) % capacity(), std::memory_order_release);
    return toRead;
}

size_t PlaybackBuffer::available() const {
    size_t rp = readPos.load(std::memory_order_acquire);
    size_t wp = writePos.load(std::memory_order_acquire);
    if (wp >= rp) return wp - rp;
    return capacity() - rp + wp;
}

void PlaybackBuffer::reset() {
    writePos.store(0, std::memory_order_release);
    readPos.store(0, std::memory_order_release);
}

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

// --- AudioEngine ---

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}
bool AudioEngine::init(const Settings& settings) {
    m_sampleRate = settings.sampleRate;
    m_bufferSize = settings.bufferSize;
    m_stereoScratch.resize(static_cast<size_t>(m_bufferSize) * 4);
    AudioClip::setStreamingThresholdFrames(
        static_cast<size_t>(settings.streamingThresholdSec) * settings.sampleRate);

    PaDeviceIndex outputDev = Pa_GetDefaultOutputDevice();
    if (settings.outputDeviceId >= 0)
        outputDev = settings.outputDeviceId;

    if (outputDev < 0) {
        qWarning() << "No audio output device available";
        return false;
    }

    const PaDeviceInfo* outInfo = Pa_GetDeviceInfo(outputDev);
    if (!outInfo) {
        qWarning() << "Invalid output device";
        return false;
    }

    PaStreamParameters outputParams;
    outputParams.device = outputDev;
    m_outputChannels = std::min(2, outInfo->maxOutputChannels);
    outputParams.channelCount = m_outputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = outInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    if (m_outputChannels < 1) {
        qWarning() << "Output device has no output channels";
        return false;
    }

    PaStreamParameters inputParams;
    PaStreamParameters* inputParamsPtr = nullptr;

    PaDeviceIndex inputDev = settings.inputDeviceId;
    if (inputDev < 0)
        inputDev = Pa_GetDefaultInputDevice();

    if (inputDev >= 0) {
        const PaDeviceInfo* inInfo = Pa_GetDeviceInfo(inputDev);
        if (inInfo && inInfo->maxInputChannels > 0) {
            m_inputChannels = std::min(2, inInfo->maxInputChannels);
            inputParams.device = inputDev;
            inputParams.channelCount = m_inputChannels;
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = inInfo->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;
            inputParamsPtr = &inputParams;
        }
    }

    // Try opening with input. If it fails, retry without input.
    PaError err = Pa_OpenStream(
        &m_stream,
        inputParamsPtr,
        &outputParams,
        m_sampleRate,
        m_bufferSize,
        paClipOff | paDitherOff,
        audioCallback,
        this
    );

    if (err != paNoError && inputParamsPtr) {
        qWarning() << "Failed to open with input, retrying without:" << Pa_GetErrorText(err);
        err = Pa_OpenStream(
            &m_stream,
            nullptr,
            &outputParams,
            m_sampleRate,
            m_bufferSize,
            paClipOff | paDitherOff,
            audioCallback,
            this
        );
    }

    if (err != paNoError) {
        qWarning() << "Failed to open audio stream:" << Pa_GetErrorText(err);
        m_stream = nullptr;
        return false;
    }

    return true;
}

void AudioEngine::shutdown() {
    stopRecording();
    stopPlayback();
    if (m_stream) {
        if (Pa_IsStreamActive(m_stream))
            Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
}

bool AudioEngine::startStream() {
    if (!m_stream) return false;
    PaError err = Pa_StartStream(m_stream);
    return err == paNoError;
}

bool AudioEngine::stopStream() {
    if (!m_stream) return false;
    PaError err = Pa_StopStream(m_stream);
    return err == paNoError;
}

bool AudioEngine::restartStream(const Settings& settings) {
    shutdown();
    return init(settings) && startStream();
}

std::vector<DeviceInfo> AudioEngine::enumerateInputDevices() {
    std::vector<DeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            DeviceInfo d;
            d.id = i;
            d.name = QString::fromUtf8(info->name);
            d.maxInputChannels = info->maxInputChannels;
            d.maxOutputChannels = info->maxOutputChannels;
            devices.push_back(d);
        }
    }
    return devices;
}

std::vector<DeviceInfo> AudioEngine::enumerateOutputDevices() {
    std::vector<DeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            DeviceInfo d;
            d.id = i;
            d.name = QString::fromUtf8(info->name);
            d.maxInputChannels = info->maxInputChannels;
            d.maxOutputChannels = info->maxOutputChannels;
            devices.push_back(d);
        }
    }
    return devices;
}

void AudioEngine::setTransportState(TransportState state) {
    // Handle recording transitions
    if (state == TransportState::Recording && !m_recordingActive) {
        startRecording();
        m_transportState.store(state, std::memory_order_release);
        return;
    }
    if (m_recordingActive && state != TransportState::Recording) {
        m_transportState.store(state, std::memory_order_release);
        stopRecording();
        return;
    }

    TransportState prev = m_transportState.load(std::memory_order_acquire);

    if (state == TransportState::Playing && prev != TransportState::Playing) {
        startPlayback();
        m_transportState.store(state, std::memory_order_release);
    } else if (state == TransportState::Stopped || state == TransportState::Paused) {
        if (prev == TransportState::Playing)
            stopPlayback();
        m_transportState.store(state, std::memory_order_release);
    } else {
        m_transportState.store(state, std::memory_order_release);
    }
}

TransportState AudioEngine::transportState() const {
    return m_transportState.load(std::memory_order_acquire);
}

void AudioEngine::setPlayPosition(int64_t pos) {
    m_playPosition.store(pos, std::memory_order_release);
    if (m_transportState.load(std::memory_order_acquire) == TransportState::Playing) {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        for (auto& stream : m_playbackStreams)
            stream.close();
        m_playbackStreams.clear();
        createPlaybackStreams();
    }
}

void AudioEngine::startRecording() {
    if (!m_project) return;
    m_recordingActive = true;
    m_recordStartSample = m_playPosition.load(std::memory_order_acquire);

    QString audioDir = m_project->audioDirectory();
    QDir().mkpath(audioDir);

    for (size_t i = 0; i < m_project->tracks().size(); ++i) {
        auto& track = m_project->tracks()[i];
        if (!track.isRecordArmed()) continue;

        QString filePath = audioDir + QString("/track_%1_%2.wav")
            .arg(i)
            .arg(QDateTime::currentMSecsSinceEpoch());

        SF_INFO info;
        std::memset(&info, 0, sizeof(info));
        info.samplerate = m_sampleRate;
        info.channels = 2;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
        if (!file) {
            qWarning() << "Failed to open recording file:" << filePath << sf_strerror(nullptr);
            continue;
        }

        RecordingTrack rt;
        rt.buffer = std::make_unique<RecordingBuffer>(static_cast<size_t>(m_sampleRate) * 30);
        rt.filePath = filePath.toStdString();
        rt.info = info;
        rt.file = file;

        qDebug() << "Recording to:" << filePath;
        m_recordingTracks.emplace(static_cast<int>(i), std::move(rt));
    }

    m_writerRunning = true;
    m_writerThread = std::thread(&AudioEngine::writerThreadFunc, this);
}

void AudioEngine::stopRecording() {
    m_recordingActive = false;

    if (m_writerThread.joinable()) {
        m_writerRunning = false;
        m_writerCond.notify_one();
        m_writerThread.join();
    }

    // Close all files and create AudioEvents
    for (auto& [trackIdx, rt] : m_recordingTracks) {
        if (rt.file) {
            sf_close(rt.file);
            rt.file = nullptr;
        }

        if (trackIdx < 0 || trackIdx >= static_cast<int>(m_project->tracks().size()))
            continue;

        auto clip = std::make_shared<AudioClip>(QString::fromStdString(rt.filePath));
        if (!clip->isValid()) {
            qWarning() << "Failed to load recorded file:" << QString::fromStdString(rt.filePath);
            continue;
        }

        AudioEvent event;
        event.clip = clip;
        event.startSample = m_recordStartSample;
        event.offsetSample = 0;
        event.durationSample = clip->frameCount();

        m_project->tracks()[trackIdx].addEvent(event);
    }

    m_recordingTracks.clear();
}

void AudioEngine::writerThreadFunc() {
    std::vector<float> tmp(8192);

    auto drain = [&] {
        for (auto& [trackIdx, rt] : m_recordingTracks) {
            if (!rt.file) continue;
            if (!rt.buffer) continue;
            size_t avail = rt.buffer->available();
            while (avail > 0) {
                size_t toRead = std::min(avail, tmp.size());
                size_t readCount = rt.buffer->read(tmp.data(), toRead);
                if (readCount == 0) break;
                sf_writef_float(rt.file, tmp.data(), readCount / 2);
                avail -= readCount;
            }
        }
    };

    while (m_writerRunning) {
        drain();
        std::unique_lock<std::mutex> lock(m_writerMutex);
        m_writerCond.wait_for(lock, std::chrono::milliseconds(30),
                              [this] { return !m_writerRunning; });
    }

    // Final drain before exit
    drain();
}

int AudioEngine::audioCallback(const void* input, void* output,
                                unsigned long frameCount,
                                const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                PaStreamCallbackFlags /*statusFlags*/,
                                void* userData) {
    auto* engine = static_cast<AudioEngine*>(userData);
    engine->processAudio(static_cast<const float*>(input),
                         static_cast<float*>(output), frameCount);
    return paContinue;
}

void AudioEngine::processAudio(const float* input, float* output,
                                unsigned long frameCount) {
    int outCh = m_outputChannels;
    int inCh = m_inputChannels;

    std::memset(output, 0, frameCount * outCh * sizeof(float));

    TransportState state = m_transportState.load(std::memory_order_acquire);

    if (state == TransportState::Recording && m_project) {
        if (input && inCh > 0) {
            for (auto& [trackIdx, rt] : m_recordingTracks) {
                if (!rt.buffer) continue;
                if (inCh == 1) {
                    for (unsigned long f = 0; f < frameCount; ++f) {
                        float s = input[f];
                        m_stereoScratch[f * 2] = s;
                        m_stereoScratch[f * 2 + 1] = s;
                    }
                    rt.buffer->write(m_stereoScratch.data(), frameCount * 2);
                } else {
                    rt.buffer->write(input, frameCount * 2);
                }
            }

            if (m_project) {
                for (const auto& track : m_project->tracks()) {
                    if (!track.isRecordArmed() || !track.isMonitoring()) continue;
                    float trackVol = track.volume() * 0.7f;

                    if (outCh >= 2) {
                        if (inCh == 1) {
                            for (unsigned long f = 0; f < frameCount; ++f) {
                                float s = input[f] * trackVol;
                                output[f * 2]     += s;
                                output[f * 2 + 1] += s;
                            }
                        } else {
                            for (unsigned long f = 0; f < frameCount; ++f) {
                                output[f * 2]     += input[f * 2]     * trackVol;
                                output[f * 2 + 1] += input[f * 2 + 1] * trackVol;
                            }
                        }
                    } else {
                        for (unsigned long f = 0; f < frameCount; ++f)
                            output[f] += input[f * inCh] * trackVol;
                    }
                }
            }

            m_writerCond.notify_one();
        }

        int64_t pos = m_playPosition.load(std::memory_order_acquire);
        m_playPosition.store(pos + frameCount, std::memory_order_release);
        return;
    }

    if (state == TransportState::Playing || state == TransportState::Paused) {
        int64_t pos = m_playPosition.load(std::memory_order_acquire);

        if (state == TransportState::Playing && m_project) {
            bool anySolo = false;
            for (const auto& track : m_project->tracks()) {
                if (track.isSolo()) { anySolo = true; break; }
            }

            for (const auto& track : m_project->tracks()) {
                if (track.isMuted()) continue;
                if (anySolo && !track.isSolo()) continue;
                float trackVol = track.volume();

                for (const auto& event : track.events()) {
                    if (!event.clip || !event.clip->isValid()) continue;

                    int64_t eventEnd = event.startSample + event.durationSample;
                    if (pos >= eventEnd || pos + frameCount <= event.startSample)
                        continue;

                    int64_t localPos = pos - event.startSample + event.offsetSample;
                    if (localPos < event.offsetSample) localPos = event.offsetSample;

                    if (event.clip->isStreaming()) {
                        std::unique_lock<std::mutex> lock(m_streamMutex, std::try_to_lock);
                        if (!lock.owns_lock()) continue;

                        // Find the corresponding playback stream (match clip + position)
                        PlaybackStream* stream = nullptr;
                        for (auto& s : m_playbackStreams) {
                            if (s.clip == event.clip.get() &&
                                s.eventStartSample == event.startSample &&
                                s.eventDurationSample == event.durationSample) {
                                stream = &s; break;
                            }
                        }
                        if (!stream || stream->finished) continue;

                        int ch = event.clip->channels();
                        size_t samplesNeeded = frameCount * ch;

                        size_t samplesRead = stream->buffer.read(m_stereoScratch.data(), samplesNeeded);
                        size_t framesAvail = samplesRead / ch;

                        for (unsigned long f = 0; f < framesAvail; ++f) {
                            float sL = m_stereoScratch[f * ch];
                            float sR = ch > 1 ? m_stereoScratch[f * ch + 1] : sL;
                            if (outCh >= 2) {
                                output[f * 2]     += sL * trackVol;
                                output[f * 2 + 1] += sR * trackVol;
                            } else {
                                output[f] += (sL + sR) * 0.5f * trackVol;
                            }
                        }

                        // Mark finished when reader is done and buffer is drained
                        if (stream->readerFinished && stream->buffer.available() == 0)
                            stream->finished = true;
                    } else {
                        const float* clipData = event.clip->data();
                        int ch = event.clip->channels();
                        size_t clipFrames = event.clip->frameCount();

                        for (unsigned long f = 0; f < frameCount; ++f) {
                            int64_t clipFrame = localPos + f;
                            if (clipFrame >= static_cast<int64_t>(clipFrames) ||
                                clipFrame >= event.offsetSample + event.durationSample)
                                continue;

                            float sL = ch >= 1 ? clipData[clipFrame * ch] : 0.0f;
                            float sR = ch >= 2 ? clipData[clipFrame * ch + 1] : sL;

                            if (outCh >= 2) {
                                output[f * 2]     += sL * trackVol;
                                output[f * 2 + 1] += sR * trackVol;
                            } else {
                                output[f] += (sL + sR) * 0.5f * trackVol;
                            }
                        }
                    }
                }
            }

            m_playPosition.store(pos + frameCount, std::memory_order_release);
        }
        return;
    }
}

void AudioEngine::startPlayback() {
    if (!m_project) return;
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        createPlaybackStreams();
    }

    m_readerRunning = true;
    m_readerThread = std::thread(&AudioEngine::readerThreadFunc, this);
}

void AudioEngine::stopPlayback() {
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

void AudioEngine::createPlaybackStreams() {
    m_playbackStreams.clear();
    int64_t pos = m_playPosition.load(std::memory_order_acquire);

    for (const auto& track : m_project->tracks()) {
        for (const auto& event : track.events()) {
            if (!event.clip || !event.clip->isValid() || !event.clip->isStreaming())
                continue;

            int64_t eventEnd = event.startSample + event.durationSample;
            if (pos >= eventEnd)
                continue;

            // If playhead is before the event start, start streaming from event's beginning
            int64_t localPos = pos - event.startSample + event.offsetSample;
            if (localPos < event.offsetSample) localPos = event.offsetSample;

            PlaybackStream stream;
            stream.clip = event.clip.get();
            stream.eventStartSample = event.startSample;
            stream.eventDurationSample = event.durationSample;
            int64_t endFrame = std::min<int64_t>(event.offsetSample + event.durationSample,
                                                  event.clip->frameCount());

            if (stream.open(event.clip->filePath(), localPos, endFrame)) {
                m_playbackStreams.push_back(std::move(stream));
            }
        }
    }
}

void AudioEngine::readerThreadFunc() {
    std::vector<float> tmp(8192 * 2);

    auto fillAll = [&] {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        for (auto& stream : m_playbackStreams) {
            if (stream.finished || stream.readerFinished || !stream.file) continue;

            size_t availSamples = stream.buffer.capacity() - stream.buffer.available() - 1;
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
        fillAll();

        std::unique_lock<std::mutex> lock(m_readerMutex);
        m_readerCond.wait_for(lock, std::chrono::milliseconds(5),
                              [this] { return !m_readerRunning; });
    }

    fillAll();
}
