#include "AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include <algorithm>
#include <cstring>
#include <QDebug>

// --- AudioEngine ---

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(const Settings& settings) {
    m_sampleRate = settings.sampleRate;
    m_bufferSize = settings.bufferSize;
    m_stereoScratch.resize(static_cast<size_t>(m_bufferSize) * 4);
    m_recordingManager.setScratchSize(static_cast<size_t>(m_bufferSize) * 2);
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
        if (m_recordingManager.isActive())
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording || prev == TransportState::Paused)
            stopPlayback();
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    // Pausing: stop capture and playback but retain position
    if (state == TransportState::Paused) {
        if (m_recordingManager.isActive())
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording)
            stopPlayback();
        m_transportState.store(TransportState::Paused, std::memory_order_release);
        return;
    }

    // Entering Recording: ensure playback, then start recording
    if (state == TransportState::Recording) {
        if (!m_recordingManager.isActive()) {
            if (prev != TransportState::Playing)
                startPlayback();
            startRecording();
        }
        m_transportState.store(TransportState::Recording, std::memory_order_release);
        return;
    }

    // Entering Playing: stop recording if active, start playback
    if (state == TransportState::Playing) {
        if (m_recordingManager.isActive())
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
        m_streamingManager.closeAll();
        auto* proj = m_project.load(std::memory_order_acquire);
        if (proj)
            m_streamingManager.createStreams(proj, pos);
    }
}

void AudioEngine::startRecording() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    m_recordingManager.start(proj, m_sampleRate, m_playPosition.load(std::memory_order_acquire));
}

void AudioEngine::stopRecording() {
    auto* proj = m_project.load(std::memory_order_acquire);
    m_recordingManager.stop(proj);
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

        if (isActive) {
            // --- Audio capture (Recording only) ---
            if (state == TransportState::Recording &&
                m_recordingManager.isRegionActive() &&
                input && inCh > 0) {
                m_recordingManager.processCapture(input, frameCount, inCh);
            }

            // Access project data under shared (read) lock — skip if writer is active
            auto* proj = m_project.load(std::memory_order_acquire);
            if (!proj) {
                m_playPosition.store(pos + frameCount, std::memory_order_release);
                return;
            }
            std::shared_lock projectLock(proj->mutex(), std::try_to_lock);
            if (!projectLock) {
                m_playPosition.store(pos + frameCount, std::memory_order_release);
                m_recordingManager.notifyWriter();
                return;
            }

            // Monitoring (only if recording and inside record region)
            if (state == TransportState::Recording &&
                m_recordingManager.isRegionActive() &&
                input && inCh > 0) {
                m_recordingManager.processMonitoring(proj, input, output, frameCount, inCh, outCh);
                m_recordingManager.notifyWriter();
            }

            // --- Playback (events) ---
            bool anySolo = false;
            for (const auto& track : proj->tracks()) {
                if (track.isSolo()) { anySolo = true; break; }
            }

            for (const auto& track : proj->tracks()) {
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
                        int ch = activeClip->channels();
                        size_t framesAvail = 0;
                        if (m_streamingManager.readEvent(activeClip.get(), event.startSample(),
                                                          event.durationSample(),
                                                          m_stereoScratch.data(), frameCount, ch,
                                                          framesAvail)) {
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
                        }
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
            if (proj->hasLoop()) {
                int64_t loopEnd = proj->loopEnd();
                if (newPos >= loopEnd) {
                    int64_t loopStart = proj->loopStart();
                    int64_t excess = newPos - loopEnd;
                    newPos = loopStart + excess;
                    if (newPos >= loopEnd)
                        newPos = loopStart;
                }
            }

            // If loop wrapped, signal reader to reset streaming stream positions
            if (newPos != pos + frameCount) {
                m_streamingManager.signalReset(newPos);
            }

            // --- Record region: control capture active flag ---
            if (state == TransportState::Recording &&
                proj->hasRecordRegion()) {
                int64_t rrStart = proj->recordRegionStart();
                int64_t rrEnd = proj->recordRegionEnd();
                bool regionActive = m_recordingManager.isRegionActive();
                if (!regionActive && newPos > rrStart && pos < rrEnd) {
                    m_recordingManager.setRegionActive(true);
                } else if (regionActive && newPos >= rrEnd) {
                    m_recordingManager.setRegionActive(false);
                }
            }

            m_playPosition.store(newPos, std::memory_order_release);
        }
    }
}

void AudioEngine::startPlayback() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    m_streamingManager.start(proj, m_playPosition.load(std::memory_order_acquire));
}

void AudioEngine::stopPlayback() {
    m_streamingManager.stop();
}
