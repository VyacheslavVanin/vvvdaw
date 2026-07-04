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

// --- AudioEngine ---

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}
bool AudioEngine::init(const Settings& settings) {
    m_sampleRate = settings.sampleRate;
    m_bufferSize = settings.bufferSize;

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
    if (state == TransportState::Recording && !m_recordingActive) {
        startRecording();
    } else if (m_recordingActive && state != TransportState::Recording) {
        stopRecording();
    }

    m_transportState.store(state, std::memory_order_release);
    if (state == TransportState::Stopped)
        m_playPosition.store(0, std::memory_order_release);
}

TransportState AudioEngine::transportState() const {
    return m_transportState.load(std::memory_order_acquire);
}

void AudioEngine::startRecording() {
    if (!m_project) return;
    m_recordingActive = true;

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

    m_stereoScratch.resize(static_cast<size_t>(m_bufferSize) * 2);
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
        event.startSample = 0;
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
            // Convert input to stereo for the recording buffer
            for (auto& [trackIdx, rt] : m_recordingTracks) {
                if (!rt.buffer) continue;
                // Interleave mono input to stereo if needed
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

            // Copy input to output (monitor)
            if (outCh == 1 && inCh >= 1) {
                for (unsigned long f = 0; f < frameCount; ++f)
                    output[f] = input[f * inCh];
            } else if (outCh >= 2 && inCh == 1) {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    output[f * 2] = input[f];
                    output[f * 2 + 1] = input[f];
                }
            } else {
                unsigned long copySamples = std::min<unsigned long>(frameCount * outCh, frameCount * inCh);
                std::memcpy(output, input, copySamples * sizeof(float));
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
            for (const auto& track : m_project->tracks()) {
                if (track.isMuted()) continue;
                float trackVol = track.volume();

                for (const auto& event : track.events()) {
                    if (!event.clip || !event.clip->isValid()) continue;

                    int64_t eventEnd = event.startSample + event.durationSample;
                    if (pos >= eventEnd || pos + frameCount <= event.startSample)
                        continue;

                    int64_t localPos = pos - event.startSample + event.offsetSample;
                    if (localPos < event.offsetSample) localPos = event.offsetSample;

                    const float* clipData = event.clip->data();
                    int clipChannels = event.clip->channels();
                    size_t clipFrames = event.clip->frameCount();

                    for (unsigned long f = 0; f < frameCount; ++f) {
                        int64_t clipFrame = localPos + f;
                        if (clipFrame >= static_cast<int64_t>(clipFrames) ||
                            clipFrame >= event.offsetSample + event.durationSample)
                            continue;

                        float sampleL = clipChannels >= 1 ? clipData[clipFrame * clipChannels] : 0.0f;
                        float sampleR = clipChannels >= 2 ? clipData[clipFrame * clipChannels + 1] : sampleL;

                        if (outCh >= 2) {
                            output[f * 2]     += sampleL * trackVol;
                            output[f * 2 + 1] += sampleR * trackVol;
                        } else {
                            output[f] += (sampleL + sampleR) * 0.5f * trackVol;
                        }
                    }
                }
            }

            m_playPosition.store(pos + frameCount, std::memory_order_release);
        }
        return;
    }
}
