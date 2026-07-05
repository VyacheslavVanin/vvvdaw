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

std::vector<DeviceInfo> AudioEngine::enumerateDevices(bool input) {
    std::vector<DeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        int channelCount = input ? info->maxInputChannels : info->maxOutputChannels;
        if (info && channelCount > 0) {
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
    TransportState prev = m_transportState.load(std::memory_order_acquire);

    // Stopping: stop everything
    if (state == TransportState::Stopped) {
        if (m_recordingActive)
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording || prev == TransportState::Paused)
            stopPlayback();
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    // Pausing: stop capture and playback but retain position
    if (state == TransportState::Paused) {
        if (m_recordingActive)
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording)
            stopPlayback();
        m_transportState.store(TransportState::Paused, std::memory_order_release);
        return;
    }

    // Entering Recording: ensure playback, then start recording
    if (state == TransportState::Recording) {
        if (!m_recordingActive) {
            if (prev != TransportState::Playing)
                startPlayback();
            startRecording();
        }
        m_transportState.store(TransportState::Recording, std::memory_order_release);
        return;
    }

    // Entering Playing: stop recording if active, start playback
    if (state == TransportState::Playing) {
        if (m_recordingActive)
            stopRecording();
        if (prev != TransportState::Playing)
            startPlayback();
        m_transportState.store(TransportState::Playing, std::memory_order_release);
        return;
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

    // If a record region exists, place recorded event at region start
    if (m_project->hasRecordRegion()) {
        m_recordStartSample = m_project->recordRegionStart();
    } else {
        m_recordStartSample = m_playPosition.load(std::memory_order_acquire);
    }

    // If no record region, capture everywhere; otherwise wait to enter region
    m_regionRecordingActive.store(!m_project->hasRecordRegion(), std::memory_order_release);

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
        rt.buffer = std::make_unique<RingBuffer>(static_cast<size_t>(m_sampleRate) * vvvdaw::RecordBufferSeconds);
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
    m_regionRecordingActive.store(false, std::memory_order_release);

    if (m_writerThread.joinable()) {
        m_writerRunning = false;
        m_writerCond.notify_one();
        m_writerThread.join();
    }

    // Close all files and create AudioEvents
    if (!m_project) {
        m_recordingTracks.clear();
        return;
    }
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

        bool addedAsTake = false;
        auto& track = m_project->tracks()[trackIdx];

        addedAsTake = processLoopRecordRegion(*clip, rt, track);

        if (!addedAsTake) {
            // Without loop+record-region: try to add as take to existing loop event
            if (m_project->hasLoop()) {
                int64_t loopStart = m_project->loopStart();
                int64_t loopEnd = m_project->loopEnd();
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
                track.addEvent(event);
            }
        }
    }

    m_recordingTracks.clear();
}

bool AudioEngine::processLoopRecordRegion(AudioClip& clip, const RecordingTrack& rt, Track& track) {
    if (!m_project->hasLoop() || !m_project->hasRecordRegion())
        return false;

    int64_t regionLen = m_project->recordRegionEnd() - m_project->recordRegionStart();
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
        if (ev.startSample() == m_recordStartSample) {
            targetEvent = &ev;
            break;
        }
    }
    if (!targetEvent) {
        AudioEvent newEvent;
        newEvent.setStartSample(m_recordStartSample);
        newEvent.setOffsetSample(0);
        newEvent.setDurationSample(regionLen);
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

void AudioEngine::writerThreadFunc() {
    std::vector<float> tmp(vvvdaw::WriterBufferSize);

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

    if (state == TransportState::Playing || state == TransportState::Recording ||
        state == TransportState::Paused) {
        int64_t pos = m_playPosition.load(std::memory_order_acquire);

        bool isActive = (state == TransportState::Playing || state == TransportState::Recording);

        if (isActive && m_project) {
            // --- Audio capture (Recording only, within record region) ---
            if (state == TransportState::Recording &&
                m_regionRecordingActive.load(std::memory_order_acquire) &&
                input && inCh > 0) {
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

                // Monitoring
                for (const auto& track : m_project->tracks()) {
                    if (!track.isRecordArmed() || !track.isMonitoring()) continue;
                    float trackVol = track.volume() * vvvdaw::MonitoringVolumeFactor;
                    float pan = track.pan();
                    float leftGain  = std::min(1.0f, 1.0f - pan);
                    float rightGain = std::min(1.0f, 1.0f + pan);

                    if (outCh >= 2) {
                        if (inCh == 1) {
                            for (unsigned long f = 0; f < frameCount; ++f) {
                                float s = input[f] * trackVol;
                                output[f * 2]     += s * leftGain;
                                output[f * 2 + 1] += s * rightGain;
                            }
                        } else {
                            for (unsigned long f = 0; f < frameCount; ++f) {
                                output[f * 2]     += input[f * 2]     * trackVol * leftGain;
                                output[f * 2 + 1] += input[f * 2 + 1] * trackVol * rightGain;
                            }
                        }
                    } else {
                        for (unsigned long f = 0; f < frameCount; ++f)
                            output[f] += input[f * inCh] * trackVol;
                    }
                }

                m_writerCond.notify_one();
            }

            // --- Playback (events) ---
            bool anySolo = false;
            for (const auto& track : m_project->tracks()) {
                if (track.isSolo()) { anySolo = true; break; }
            }

            for (const auto& track : m_project->tracks()) {
                if (track.isMuted()) continue;
                if (anySolo && !track.isSolo()) continue;
                float trackVol = track.volume();
                float pan = track.pan();
                float leftGain  = std::min(1.0f, 1.0f - pan);
                float rightGain = std::min(1.0f, 1.0f + pan);

                for (const auto& event : track.events()) {
                    auto activeClip = event.activeClip();
                    if (!activeClip || !activeClip->isValid()) continue;

                    int64_t eventEnd = event.startSample() + event.durationSample();
                    if (pos >= eventEnd || pos + frameCount <= event.startSample())
                        continue;

                    int64_t localPos = pos - event.startSample() + event.offsetSample();
                    if (localPos < event.offsetSample()) localPos = event.offsetSample();

                    if (activeClip->isStreaming()) {
                        std::unique_lock<std::mutex> lock(m_streamMutex, std::try_to_lock);
                        if (!lock.owns_lock()) continue;

                        PlaybackStream* stream = nullptr;
                        for (auto& s : m_playbackStreams) {
                            if (s.clip == activeClip.get() &&
                                s.eventStartSample == event.startSample() &&
                                s.eventDurationSample == event.durationSample()) {
                                stream = &s; break;
                            }
                        }
                        if (!stream || stream->finished) continue;

                        int ch = activeClip->channels();
                        size_t samplesNeeded = frameCount * ch;

                        size_t samplesRead = stream->buffer.read(m_stereoScratch.data(), samplesNeeded);
                        size_t framesAvail = samplesRead / ch;

                        for (unsigned long f = 0; f < framesAvail; ++f) {
                            float sL = m_stereoScratch[f * ch];
                            float sR = ch > 1 ? m_stereoScratch[f * ch + 1] : sL;
                            if (outCh >= 2) {
                                output[f * 2]     += sL * trackVol * leftGain;
                                output[f * 2 + 1] += sR * trackVol * rightGain;
                            } else {
                                output[f] += (sL + sR) * 0.5f * trackVol;
                            }
                        }

                        if (stream->readerFinished && stream->buffer.available() == 0)
                            stream->finished = true;
                    } else {
                        const float* clipData = activeClip->data();
                        int ch = activeClip->channels();
                        size_t clipFrames = activeClip->frameCount();

                        for (unsigned long f = 0; f < frameCount; ++f) {
                            int64_t clipFrame = localPos + f;
                            if (clipFrame >= static_cast<int64_t>(clipFrames) ||
                                clipFrame >= event.offsetSample() + event.durationSample())
                                continue;

                            float sL = ch >= 1 ? clipData[clipFrame * ch] : 0.0f;
                            float sR = ch >= 2 ? clipData[clipFrame * ch + 1] : sL;

                            if (outCh >= 2) {
                                output[f * 2]     += sL * trackVol * leftGain;
                                output[f * 2 + 1] += sR * trackVol * rightGain;
                            } else {
                                output[f] += (sL + sR) * 0.5f * trackVol;
                            }
                        }
                    }
                }
            }

            // --- Advance playhead ---
            int64_t newPos = pos + frameCount;

            // Loop wrapping
            if (m_project && m_project->hasLoop()) {
                int64_t loopEnd = m_project->loopEnd();
                if (newPos >= loopEnd) {
                    int64_t loopStart = m_project->loopStart();
                    int64_t excess = newPos - loopEnd;
                    newPos = loopStart + excess;
                    if (newPos >= loopEnd)
                        newPos = loopStart;
                }
            }

            // If loop wrapped, signal reader to reset streaming stream positions
            if (newPos != pos + frameCount) {
                m_needStreamReset.store(true, std::memory_order_release);
                m_resetStreamPos.store(newPos, std::memory_order_release);
                m_readerCond.notify_one();
            }

            // --- Record region: control capture active flag ---
            if (state == TransportState::Recording &&
                m_project && m_project->hasRecordRegion()) {
                int64_t rrStart = m_project->recordRegionStart();
                int64_t rrEnd = m_project->recordRegionEnd();
                bool regionActive = m_regionRecordingActive.load(std::memory_order_acquire);
                if (!regionActive && newPos > rrStart && pos < rrEnd) {
                    // Entered or already inside the region
                    m_regionRecordingActive.store(true, std::memory_order_release);
                } else if (regionActive && newPos >= rrEnd) {
                    m_regionRecordingActive.store(false, std::memory_order_release);
                }
            }

            m_playPosition.store(newPos, std::memory_order_release);
        }
    }
}

void AudioEngine::startPlayback() {
    if (!m_project) return;

    // Guard against double invocation: stop any existing reader thread first
    if (m_readerThread.joinable()) {
        m_readerRunning = false;
        m_readerCond.notify_one();
        m_readerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        createPlaybackStreams();
    }

    m_readerRunning = true;
    try {
        m_readerThread = std::thread(&AudioEngine::readerThreadFunc, this);
    } catch (const std::system_error& e) {
        qWarning() << "Failed to start reader thread:" << e.what();
        m_readerRunning = false;
    }
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
            auto activeClip = event.activeClip();
            if (!activeClip || !activeClip->isValid() || !activeClip->isStreaming())
                continue;

            int64_t eventEnd = event.startSample() + event.durationSample();
            if (pos >= eventEnd)
                continue;

            // If playhead is before the event start, start streaming from event's beginning
            int64_t localPos = pos - event.startSample() + event.offsetSample();
            if (localPos < event.offsetSample()) localPos = event.offsetSample();

            PlaybackStream stream;
            stream.clip = activeClip.get();
            stream.eventStartSample = event.startSample();
            stream.eventOffsetSample = event.offsetSample();
            stream.eventDurationSample = event.durationSample();
            int64_t endFrame = std::min<int64_t>(event.offsetSample() + event.durationSample(),
                                                  activeClip->frameCount());

            if (stream.open(activeClip->filePath(), localPos, endFrame)) {
                m_playbackStreams.push_back(std::move(stream));
            }
        }
    }
}

void AudioEngine::readerThreadFunc() {
    std::vector<float> tmp(vvvdaw::ReaderBufferSize);

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
        // Handle stream reset requests from loop wrapping
        if (m_needStreamReset.load(std::memory_order_acquire)) {
            int64_t resetPos = m_resetStreamPos.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lock(m_streamMutex);
                for (auto& stream : m_playbackStreams) {
                    int64_t newLocalPos = resetPos - stream.eventStartSample + stream.eventOffsetSample;
                    if (newLocalPos < stream.eventOffsetSample)
                        newLocalPos = stream.eventOffsetSample;
                    stream.resetPosition(newLocalPos);
                }
            }
            m_needStreamReset.store(false, std::memory_order_release);
        }

        fillAll();

        std::unique_lock<std::mutex> lock(m_readerMutex);
        m_readerCond.wait_for(lock, std::chrono::milliseconds(5),
                              [this] { return !m_readerRunning; });
    }

    fillAll();
}
