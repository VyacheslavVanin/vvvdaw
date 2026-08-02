#include "AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioClip.h"
#include "model/AudioEvent.h"
#include "model/Instrument.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "AudioUtils.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <QDebug>

using vvvdaw::TransportState;

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(const Settings& settings) {
    m_sampleRate = settings.sampleRate;
    m_bufferSize = settings.bufferSize;
    m_stereoScratch.resize(static_cast<size_t>(m_bufferSize) * 4);
    m_trackScratch.resize(static_cast<size_t>(m_bufferSize) * 2, 0.0f);
    m_sourceScratch.resize((static_cast<size_t>(m_bufferSize) * 4 + TimeStretch::kWindowSize) * 2, 0.0f);
    m_busDeinterleaveL.resize(static_cast<size_t>(m_bufferSize), 0.0f);
    m_busDeinterleaveR.resize(static_cast<size_t>(m_bufferSize), 0.0f);
    m_instrumentScratchL.resize(static_cast<size_t>(m_bufferSize), 0.0f);
    m_instrumentScratchR.resize(static_cast<size_t>(m_bufferSize), 0.0f);
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

    m_clickEnvelopeSize = m_sampleRate * 5 / 1000;
    m_clickEnvelope.resize(m_clickEnvelopeSize);
    for (int i = 0; i < m_clickEnvelopeSize; ++i) {
        double t = static_cast<double>(i) / m_sampleRate;
        double decay = std::exp(-t * 800.0);
        m_clickEnvelope[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t) * decay);
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

    if (state == TransportState::Stopped) {
        if (m_recordingManager.isActive())
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording || prev == TransportState::Paused || prev == TransportState::Precounting)
            stopPlayback();
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    if (state == TransportState::Paused) {
        if (m_recordingManager.isActive())
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording)
            stopPlayback();
        m_transportState.store(TransportState::Paused, std::memory_order_release);
        return;
    }

    if (state == TransportState::Recording) {
        if (!m_recordingManager.isActive()) {
            if (m_precountEnabled && (prev == TransportState::Stopped || prev == TransportState::Paused)) {
                startPrecount();
                m_transportState.store(TransportState::Precounting, std::memory_order_release);
                return;
            }
            if (prev != TransportState::Playing)
                startPlayback();
            startRecording();
        }
        m_transportState.store(TransportState::Recording, std::memory_order_release);
        return;
    }

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
        clearStretchSlots();
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

    if (state == TransportState::Precounting) {
        auto* proj = m_project.load(std::memory_order_acquire);
        if (!proj) return;
        std::shared_lock projectLock(proj->mutex(), std::try_to_lock);
        if (!projectLock) return;
        processPrecounting(proj, output, frameCount, outCh);
        return;
    }

    if (state == TransportState::Playing || state == TransportState::Recording ||
        state == TransportState::Paused) {
        int64_t pos = m_playPosition.load(std::memory_order_acquire);

        bool isActive = (state == TransportState::Playing || state == TransportState::Recording);

        if (isActive) {
            if (state == TransportState::Recording &&
                m_recordingManager.isRegionActive() &&
                input && inCh > 0) {
                m_recordingManager.processCapture(input, frameCount, inCh);
            }

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

            processBusMixing(proj, output, frameCount, pos, outCh, input, inCh);

            if (state == TransportState::Recording)
                m_recordingManager.notifyWriter();

            int64_t newPos = advancePlayhead(proj, pos, frameCount, state);

            m_playPosition.store(newPos, std::memory_order_release);
        }
    }

    if ((state == TransportState::Stopped || state == TransportState::Paused)
        && input && inCh > 0) {
        auto* proj = m_project.load(std::memory_order_acquire);
        if (!proj) return;
        std::shared_lock projectLock(proj->mutex(), std::try_to_lock);
        if (!projectLock) return;
        int64_t pos = m_playPosition.load(std::memory_order_acquire);
        processBusMixing(proj, output, frameCount, pos, outCh, input, inCh, true);
    }
}

void AudioEngine::rebuildBusGraph(Project* proj) {
    int busCount = static_cast<int>(proj->buses().size());
    m_busCount = busCount;

    m_busBuffers.resize(busCount);
    for (int i = 0; i < busCount; ++i)
        m_busBuffers[i].resize(static_cast<size_t>(m_bufferSize) * 2, 0.0f);

    std::vector<int> inDegree(busCount, 0);
    for (int i = 0; i < busCount; ++i) {
        int parent = proj->buses()[i].outputBusIndex;
        if (parent >= 0 && parent < busCount && parent != i)
            inDegree[parent]++;
    }

    m_busProcessOrder.clear();
    m_busProcessOrder.reserve(busCount);
    std::vector<int> queue;
    for (int i = 0; i < busCount; ++i) {
        if (inDegree[i] == 0)
            queue.push_back(i);
    }

    while (!queue.empty()) {
        int node = queue.back();
        queue.pop_back();
        m_busProcessOrder.push_back(node);

        int parent = proj->buses()[node].outputBusIndex;
        if (parent >= 0 && parent < busCount && parent != node) {
            inDegree[parent]--;
            if (inDegree[parent] == 0)
                queue.push_back(parent);
        }
    }

    if (static_cast<int>(m_busProcessOrder.size()) < busCount) {
        for (int i = 0; i < busCount; ++i) {
            if (std::find(m_busProcessOrder.begin(), m_busProcessOrder.end(), i) == m_busProcessOrder.end()) {
                m_busProcessOrder.push_back(i);
            }
        }
    }
}

void AudioEngine::clearStretchSlots() {
    m_stretchSlots.clear();
}

void AudioEngine::flushActiveMidiNotes(Project* proj, unsigned long frameCount) {
    for (auto& an : m_activeMidiNotes) {
        if (an.toInstrument && an.destIndex >= 0 &&
            an.destIndex < static_cast<int>(proj->instruments().size())) {
            MidiMessage m;
            m.sampleOffset = 0;
            m.status = static_cast<uint8_t>(0x80 | an.channel);
            m.data1 = an.pitch;
            m.data2 = 0;
            m_instrumentMidi[an.destIndex].push_back(m);
        } else if (!an.toInstrument && an.destIndex >= 0) {
            m_midiOutput.send(an.destIndex, static_cast<uint8_t>(0x80 | an.channel), an.pitch, 0);
        }
    }
    m_activeMidiNotes.clear();
    (void)frameCount;
}

void AudioEngine::scheduleMidiTracks(Project* proj, unsigned long frameCount, int64_t pos) {
    bool anySolo = false;
    for (const auto& track : proj->tracks()) {
        if (track.isSolo()) { anySolo = true; break; }
    }

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.type() != Track::Type::Midi) { ++trackIndex; continue; }
        if (track.isMuted()) { ++trackIndex; continue; }
        if (anySolo && !track.isSolo()) { ++trackIndex; continue; }

        int instIdx = track.instrumentIndex();
        bool toInstrument = (instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size()));
        int deviceId = track.midiOutputDeviceId();
        int destIdx = toInstrument ? instIdx : deviceId;
        uint8_t channel = static_cast<uint8_t>(trackIndex % 16);

        for (const auto& event : track.midiEvents()) {
            auto clip = event.activeClip();
            if (!clip) continue;

            int64_t eventEnd = event.startSample() + event.durationSample();
            if (pos >= eventEnd || pos + static_cast<int64_t>(frameCount) <= event.startSample())
                continue;

            int64_t offsetTicks = proj->samplesToTicks(event.offsetSample());
            for (const auto& note : clip->notes()) {
                int64_t noteStart = event.startSample()
                    + proj->ticksToSamples(note.startTick - offsetTicks);
                int64_t noteEnd = event.startSample()
                    + proj->ticksToSamples(note.endTick() - offsetTicks);
                if (noteEnd <= pos || noteStart >= pos + static_cast<int64_t>(frameCount))
                    continue;

                bool alreadyActive = false;
                for (auto& an : m_activeMidiNotes) {
                    if (an.trackIndex == trackIndex && an.eventId == event.id()
                        && an.noteId == note.id) {
                        alreadyActive = true;
                        break;
                    }
                }

                if (!alreadyActive) {
                    int off = static_cast<int>(std::max<int64_t>(0, noteStart - pos));
                    if (off < static_cast<int>(frameCount)) {
                        if (toInstrument) {
                            MidiMessage m;
                            m.sampleOffset = off;
                            m.status = static_cast<uint8_t>(0x90 | channel);
                            m.data1 = static_cast<uint8_t>(note.pitch);
                            m.data2 = static_cast<uint8_t>(note.velocity);
                            m_instrumentMidi[instIdx].push_back(m);
                        } else if (deviceId >= 0) {
                            m_midiOutput.send(deviceId, static_cast<uint8_t>(0x90 | channel),
                                              static_cast<uint8_t>(note.pitch),
                                              static_cast<uint8_t>(note.velocity));
                        }
                        ActiveMidiNote an;
                        an.trackIndex = trackIndex;
                        an.eventId = event.id();
                        an.noteId = note.id;
                        an.destIndex = destIdx;
                        an.toInstrument = toInstrument;
                        an.channel = channel;
                        an.pitch = static_cast<uint8_t>(note.pitch);
                        m_activeMidiNotes.push_back(an);
                    }
                }

                if (noteEnd >= pos && noteEnd < pos + static_cast<int64_t>(frameCount)) {
                    int off = static_cast<int>(noteEnd - pos);
                    if (toInstrument) {
                        MidiMessage m;
                        m.sampleOffset = off;
                        m.status = static_cast<uint8_t>(0x80 | channel);
                        m.data1 = static_cast<uint8_t>(note.pitch);
                        m.data2 = 0;
                        m_instrumentMidi[instIdx].push_back(m);
                    } else if (deviceId >= 0) {
                        m_midiOutput.send(deviceId, static_cast<uint8_t>(0x80 | channel),
                                          static_cast<uint8_t>(note.pitch), 0);
                    }
                    auto it = std::remove_if(m_activeMidiNotes.begin(), m_activeMidiNotes.end(),
                        [&](const ActiveMidiNote& a) {
                            return a.trackIndex == trackIndex && a.eventId == event.id()
                                && a.noteId == note.id;
                        });
                    m_activeMidiNotes.erase(it, m_activeMidiNotes.end());
                }
            }
        }
        ++trackIndex;
    }
}

void AudioEngine::processInstruments(Project* proj, unsigned long frameCount) {
    int instCount = static_cast<int>(proj->instruments().size());
    if (instCount == 0) return;

    bool anySolo = false;
    for (auto& inst : proj->instruments()) {
        if (inst.isSolo()) { anySolo = true; break; }
    }

    int busCount = static_cast<int>(proj->buses().size());
    for (int i = 0; i < instCount; ++i) {
        auto& inst = proj->instruments()[i];
        if (inst.isMuted() || !inst.synth()) continue;
        if (anySolo && !inst.isSolo()) continue;

        int busIdx = inst.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;
        float* busBuf = m_busBuffers[busIdx].data();
        auto [lGain, rGain] = panGains(inst.pan());

        std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.begin() + frameCount, 0.0f);
        std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.begin() + frameCount, 0.0f);
        float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
        float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };

        inst.synth()->process(inBufs, outBufs, frameCount, 2, &m_instrumentMidi[i]);
        if (inst.effects().count() > 0)
            inst.effects().process(inBufs, outBufs, frameCount, 2);

        for (unsigned long f = 0; f < frameCount; ++f) {
            busBuf[f * 2]     += m_instrumentScratchL[f] * inst.volume() * lGain;
            busBuf[f * 2 + 1] += m_instrumentScratchR[f] * inst.volume() * rGain;
        }
    }
}

size_t AudioEngine::readEventBlock(const AudioEvent& event, int trackIndex,
                                   int64_t pos, float* out, size_t outFrames) {
    auto clip = event.activeClip();
    if (!clip || !clip->isValid())
        return 0;

    const int64_t duration = event.durationSample();
    int64_t srcFrames = event.sourceFrames();
    if (srcFrames <= 0)
        srcFrames = duration;
    if (duration <= 0 || srcFrames <= 0)
        return 0;

    const int64_t offset = event.offsetSample();
    const int64_t clipFrames = static_cast<int64_t>(clip->frameCount());
    const int64_t availSource = std::max<int64_t>(
        0, std::min<int64_t>(srcFrames, clipFrames - offset));

    const int ch = clip->channels();
    if (ch <= 0)
        return 0;

    const double rate = srcFrames / static_cast<double>(duration);

    if (std::fabs(rate - 1.0) < 1e-3) {
        if (clip->isStreaming()) {
            size_t framesAvail = 0;
            if (!m_streamingManager.readEvent(clip.get(), event.startSample(), duration,
                                              out, outFrames, ch, framesAvail))
                return 0;
            return framesAvail;
        }
        int64_t localPos = pos - event.startSample() + offset;
        if (localPos < offset)
            localPos = offset;
        int64_t limit = offset + availSource;
        int64_t validFrames = std::min<int64_t>(clipFrames, limit) - localPos;
        if (validFrames <= 0)
            return 0;
        size_t toCopy = static_cast<size_t>(std::min<int64_t>(
            validFrames, static_cast<int64_t>(outFrames)));
        std::memcpy(out, clip->data() + localPos * ch,
                    toCopy * static_cast<size_t>(ch) * sizeof(float));
        return toCopy;
    }

    const int64_t slotKey = (static_cast<int64_t>(trackIndex) << 32) |
                            (event.id() & 0xFFFFFFFFLL);
    auto& slot = m_stretchSlots[slotKey];
    bool freshSlot = false;
    if (slot.clip != clip.get() || slot.offset != offset || slot.srcFrames != srcFrames ||
        slot.duration != duration) {
        slot.clip = clip.get();
        slot.offset = offset;
        slot.srcFrames = srcFrames;
        slot.duration = duration;
        slot.ts.setChannels(ch);
        slot.ts.setSampleRate(m_sampleRate);
        slot.ts.reset();
        slot.srcCursor = offset + static_cast<int64_t>(
            std::llround(static_cast<double>(pos - event.startSample()) * rate));
        if (slot.srcCursor < offset)
            slot.srcCursor = offset;
        freshSlot = true;
    }

    size_t needIn = static_cast<size_t>(
        std::ceil(static_cast<double>(outFrames) * rate));
    if (freshSlot)
        needIn += TimeStretch::kWindowSize;
    int64_t remaining = (offset + availSource) - slot.srcCursor;
    if (remaining <= 0)
        return 0;
    size_t pushFrames = static_cast<size_t>(
        std::min<int64_t>(static_cast<int64_t>(needIn), remaining));
    size_t scratchFrames = m_sourceScratch.size() / static_cast<size_t>(ch);
    if (pushFrames > scratchFrames)
        pushFrames = scratchFrames;

    size_t pushed = 0;
    if (clip->isStreaming()) {
        if (pushFrames > 0 &&
            m_streamingManager.readEvent(clip.get(), event.startSample(), duration,
                                         m_sourceScratch.data(), pushFrames, ch, pushed)) {
            // pushed filled
        }
    } else {
        if (pushFrames > 0) {
            const float* src = clip->data() + slot.srcCursor * ch;
            std::memcpy(m_sourceScratch.data(), src,
                        pushFrames * static_cast<size_t>(ch) * sizeof(float));
            pushed = pushFrames;
        }
    }

    if (pushed > 0)
        slot.ts.push(m_sourceScratch.data(), pushed);
    slot.srcCursor += static_cast<int64_t>(pushed);

    return slot.ts.pull(out, outFrames, rate);
}

void AudioEngine::processBusMixing(Project* proj, float* output, unsigned long frameCount,
                                    int64_t pos, int outCh, const float* input, int inCh,
                                    bool monitoringOnly) {
    int busCount = static_cast<int>(proj->buses().size());
    if (busCount == 0) return;

    if (busCount != m_busCount)
        rebuildBusGraph(proj);

    for (int i = 0; i < busCount; ++i)
        std::fill(m_busBuffers[i].begin(), m_busBuffers[i].end(), 0.0f);

    int instCount = static_cast<int>(proj->instruments().size());
    if (instCount != m_instrumentCount) {
        m_instrumentMidi.resize(static_cast<size_t>(instCount));
        for (auto& b : m_instrumentMidi)
            b.reserve(256);
        m_instrumentCount = instCount;
    }
    for (auto& b : m_instrumentMidi)
        b.clear();

    bool firstActiveBlock = !m_midiTransportActive && !monitoringOnly;
    bool midiJumped = m_midiTransportActive &&
        (pos < m_lastMidiPos || pos - m_lastMidiPos > static_cast<int64_t>(frameCount) * 2);
    if ((firstActiveBlock && !m_activeMidiNotes.empty()) || midiJumped)
        flushActiveMidiNotes(proj, frameCount);
    m_midiTransportActive = !monitoringOnly;
    m_lastMidiPos = pos;

    bool anySolo = false;
    for (const auto& track : proj->tracks()) {
        if (track.isSolo()) { anySolo = true; break; }
    }

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.isMuted()) { ++trackIndex; continue; }
        if (anySolo && !track.isSolo()) { ++trackIndex; continue; }

        int busIdx = track.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;

        float trackVol = track.volume();
        float pan = track.pan();
        auto [leftGain, rightGain] = panGains(pan);

        bool isMono = track.channels() < 2;
        bool hasPlugins = track.pluginChain().count() > 0;
        bool hasAnyEvent = false;

        float* trackL = m_trackScratch.data();
        float* trackR = m_trackScratch.data() + frameCount;
        if (hasPlugins)
            std::fill(m_trackScratch.begin(), m_trackScratch.begin() + frameCount * 2, 0.0f);

        float* busBuf = m_busBuffers[busIdx].data();

        bool hasMonitor = track.isMonitoring() && input && inCh > 0;
        if (hasMonitor) {
            if (hasPlugins) {
                addSourceToTrack(trackL, trackR, input, inCh, frameCount, isMono);
                hasAnyEvent = true;
            } else {
                addSourceToBus(busBuf, input, inCh, frameCount, trackVol,
                               leftGain, rightGain, isMono);
            }
        }

        if (!monitoringOnly) {
            for (const auto& event : track.events()) {
                auto activeClip = event.activeClip();
                if (!activeClip || !activeClip->isValid()) continue;

                int64_t eventEnd = event.startSample() + event.durationSample();
                if (pos >= eventEnd || pos + frameCount <= event.startSample())
                    continue;

                hasAnyEvent = true;
                size_t framesAvail = readEventBlock(event, trackIndex, pos,
                                                    m_stereoScratch.data(),
                                                    frameCount);
                if (framesAvail > 0) {
                    int ch = activeClip->channels();
                    if (hasPlugins) {
                        addSourceToTrack(trackL, trackR, m_stereoScratch.data(), ch,
                                         framesAvail, isMono);
                    } else {
                        addSourceToBus(busBuf, m_stereoScratch.data(), ch,
                                       framesAvail, trackVol, leftGain, rightGain, isMono);
                    }
                }
            }
        }

        if (hasPlugins && hasAnyEvent) {
            float* inBufs[2] = { trackL, trackR };
            float* outBufs[2] = { trackL, trackR };
            int pluginChannels = isMono ? 1 : 2;
            track.pluginChain().process(inBufs, outBufs, frameCount, pluginChannels);

            writeTrackToBus(busBuf, trackL, trackR, frameCount, trackVol,
                            leftGain, rightGain, isMono);
        }
        ++trackIndex;
    }

    if (!monitoringOnly)
        scheduleMidiTracks(proj, frameCount, pos);

    if (!monitoringOnly)
        processInstruments(proj, frameCount);

    if (m_metronomeEnabled && !monitoringOnly) {
        int metroIdx = 1;
        if (metroIdx < busCount) {
            generateClick(proj, m_busBuffers[metroIdx].data(), frameCount, pos, outCh);
        }
    }

    bool anyBusSolo = false;
    for (const auto& bus : proj->buses()) {
        if (bus.solo) { anyBusSolo = true; break; }
    }

    for (int idx : m_busProcessOrder) {
        const auto& bus = proj->buses()[idx];

        if (bus.pluginChain.count() > 0) {
            float* buf = m_busBuffers[idx].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                m_busDeinterleaveL[f] = buf[f * 2];
                m_busDeinterleaveR[f] = buf[f * 2 + 1];
            }
            float* inBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            float* outBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            bus.pluginChain.process(inBufs, outBufs, frameCount, 2);
            for (unsigned long f = 0; f < frameCount; ++f) {
                buf[f * 2]     = m_busDeinterleaveL[f];
                buf[f * 2 + 1] = m_busDeinterleaveR[f];
            }
        }

        if (bus.muted) continue;
        if (anyBusSolo && !bus.solo) continue;

        auto [bLeftGain, bRightGain] = panGains(bus.pan);
        float bVol = bus.volume;

        int parentIdx = bus.outputBusIndex;
        bool routeToOutput = (parentIdx < 0 || parentIdx >= busCount);

        if (routeToOutput) {
            float* buf = m_busBuffers[idx].data();
            if (outCh >= 2) {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    output[f * 2]     += buf[f * 2]     * bVol * bLeftGain;
                    output[f * 2 + 1] += buf[f * 2 + 1] * bVol * bRightGain;
                }
            } else {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    output[f] += (buf[f * 2] + buf[f * 2 + 1]) * 0.5f * bVol;
                }
            }
        } else {
            float* srcBuf = m_busBuffers[idx].data();
            float* dstBuf = m_busBuffers[parentIdx].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                dstBuf[f * 2]     += srcBuf[f * 2]     * bVol * bLeftGain;
                dstBuf[f * 2 + 1] += srcBuf[f * 2 + 1] * bVol * bRightGain;
            }
        }
    }
}

void AudioEngine::mixPlayback(Project* proj, float* output, unsigned long frameCount,
                               int64_t pos, int outCh) {
    bool anySolo = false;
    for (const auto& track : proj->tracks()) {
        if (track.isSolo()) { anySolo = true; break; }
    }

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.isMuted()) { ++trackIndex; continue; }
        if (anySolo && !track.isSolo()) { ++trackIndex; continue; }
        float trackVol = track.volume();
        float pan = track.pan();
        auto [leftGain, rightGain] = panGains(pan);

        for (const auto& event : track.events()) {
            auto activeClip = event.activeClip();
            if (!activeClip || !activeClip->isValid()) continue;

            int64_t eventEnd = event.startSample() + event.durationSample();
            if (pos >= eventEnd || pos + frameCount <= event.startSample())
                continue;

            int ch = activeClip->channels();
            size_t framesAvail = readEventBlock(event, trackIndex, pos,
                                                m_stereoScratch.data(),
                                                frameCount);
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
        ++trackIndex;
    }
}

int64_t AudioEngine::advancePlayhead(Project* proj, int64_t pos, unsigned long frameCount,
                                      TransportState state) {
    int64_t newPos = pos + frameCount;

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

    if (newPos != pos + frameCount) {
        clearStretchSlots();
        m_streamingManager.signalReset(newPos);
    }

    if (state == TransportState::Recording && proj->hasRecordRegion()) {
        int64_t rrStart = proj->recordRegionStart();
        int64_t rrEnd = proj->recordRegionEnd();
        bool regionActive = m_recordingManager.isRegionActive();
        if (!regionActive && newPos > rrStart && pos < rrEnd)
            m_recordingManager.setRegionActive(true);
        else if (regionActive && newPos >= rrEnd)
            m_recordingManager.setRegionActive(false);
    }

    return newPos;
}

void AudioEngine::startPlayback() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    clearStretchSlots();
    refreshMidiOutputs();
    activateAllPlugins();
    m_streamingManager.start(proj, m_playPosition.load(std::memory_order_acquire));
}

void AudioEngine::stopPlayback() {
    m_streamingManager.stop();
    clearStretchSlots();
    panicMidi();
    m_activeMidiNotes.clear();
}

void AudioEngine::refreshMidiOutputs() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;

    std::set<int> needed;
    for (const auto& track : proj->tracks()) {
        if (track.type() == Track::Type::Midi && track.midiOutputDeviceId() >= 0)
            needed.insert(track.midiOutputDeviceId());
    }

    for (int id : needed) {
        if (!m_openMidiDevices.count(id))
            m_midiOutput.open(id);
    }
    for (int id : m_openMidiDevices) {
        if (!needed.count(id))
            m_midiOutput.close(id);
    }
    m_openMidiDevices = std::move(needed);
}

void AudioEngine::panicMidi() {
    for (int id : m_openMidiDevices)
        m_midiOutput.sendAllNotesOff(id);
}

void AudioEngine::generateClick(Project* proj, float* buffer, unsigned long frameCount,
                                 int64_t pos, int outCh) {
    double samplesPerBeat = proj->samplesPerBeat();
    double samplesPerBar = proj->samplesPerBar();
    if (samplesPerBeat <= 0) return;

    for (unsigned long f = 0; f < frameCount; ++f) {
        int64_t samplePos = pos + f;
        double beatInBar = std::fmod(static_cast<double>(samplePos), samplesPerBar) / samplesPerBeat;
        int beatNum = static_cast<int>(std::floor(beatInBar));
        double beatFrac = beatInBar - std::floor(beatInBar);

        bool isDownbeat = (beatNum == 0);

        if (beatFrac < 1.0 / samplesPerBeat && m_clickPlayhead < 0) {
            m_clickPlayhead = 0;
            m_clickIsDownbeat = isDownbeat;
        }

        if (m_clickPlayhead >= 0 && m_clickPlayhead < m_clickEnvelopeSize) {
            float clickSample = m_clickEnvelope[m_clickPlayhead];
            if (!m_clickIsDownbeat)
                clickSample *= 0.6f;
            buffer[f * 2]     += clickSample;
            buffer[f * 2 + 1] += clickSample;
            m_clickPlayhead++;
        } else {
            m_clickPlayhead = -1;
        }
    }
}

void AudioEngine::processPrecounting(Project* proj, float* output, unsigned long frameCount,
                                      int outCh) {
    if (m_precountTotalSamples <= 0) {
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    double samplesPerBeat = proj->samplesPerBeat();
    double samplesPerBar = proj->samplesPerBar();
    if (samplesPerBeat <= 0) return;

    float metroVol = 1.0f;
    if (static_cast<int>(proj->buses().size()) > 1)
        metroVol = proj->buses()[1].volume;

    for (unsigned long f = 0; f < frameCount; ++f) {
        if (m_precountPosition >= m_precountTotalSamples) break;

        double beatInBar = std::fmod(static_cast<double>(m_precountPosition), samplesPerBar) / samplesPerBeat;
        int beatNum = static_cast<int>(std::floor(beatInBar));
        double beatFrac = beatInBar - std::floor(beatInBar);

        bool isDownbeat = (beatNum == 0);

        if (beatFrac < 1.0 / samplesPerBeat && m_clickPlayhead < 0) {
            m_clickPlayhead = 0;
            m_clickIsDownbeat = isDownbeat;
        }

        if (m_clickPlayhead >= 0 && m_clickPlayhead < m_clickEnvelopeSize) {
            float clickSample = m_clickEnvelope[m_clickPlayhead];
            if (!m_clickIsDownbeat)
                clickSample *= 0.6f;
            output[f * 2]     += clickSample * metroVol;
            output[f * 2 + 1] += clickSample * metroVol;
            m_clickPlayhead++;
        } else {
            m_clickPlayhead = -1;
        }

        m_precountPosition++;
    }

    if (m_precountPosition >= m_precountTotalSamples) {
        m_playPosition.store(m_precountStartPlayhead, std::memory_order_release);
        startPlayback();
        startRecording();
        m_transportState.store(TransportState::Recording, std::memory_order_release);
    }
}

void AudioEngine::startPrecount() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    m_precountStartPlayhead = m_playPosition.load(std::memory_order_acquire);
    m_precountTotalSamples = static_cast<int64_t>(proj->samplesPerBar());
    m_precountPosition = 0;
    m_clickPlayhead = -1;
}

void AudioEngine::activateAllPlugins() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;

    for (auto& track : proj->tracks())
        activatePluginChain(const_cast<PluginChain&>(track.pluginChain()));
    for (auto& bus : proj->buses())
        activatePluginChain(bus.pluginChain);
    for (auto& inst : proj->instruments()) {
        if (inst.synth() && !inst.synth()->isActive())
            inst.synth()->activate(m_sampleRate, m_bufferSize);
        activatePluginChain(inst.effects());
    }
}

void AudioEngine::deactivateAllPlugins() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;

    for (auto& track : proj->tracks()) {
        auto& chain = const_cast<PluginChain&>(track.pluginChain());
        for (int i = 0; i < chain.count(); ++i) {
            auto* plugin = chain.plugin(i);
            if (plugin && plugin->isActive())
                plugin->deactivate();
        }
    }
    for (auto& bus : proj->buses()) {
        for (int i = 0; i < bus.pluginChain.count(); ++i) {
            auto* plugin = bus.pluginChain.plugin(i);
            if (plugin && plugin->isActive())
                plugin->deactivate();
        }
    }
    for (auto& inst : proj->instruments()) {
        if (inst.synth() && inst.synth()->isActive())
            inst.synth()->deactivate();
        for (int i = 0; i < inst.effects().count(); ++i) {
            auto* plugin = inst.effects().plugin(i);
            if (plugin && plugin->isActive())
                plugin->deactivate();
        }
    }
}

void AudioEngine::activatePluginChain(PluginChain& chain) {
    for (int i = 0; i < chain.count(); ++i) {
        auto* plugin = chain.plugin(i);
        if (plugin && !plugin->isActive())
            plugin->activate(m_sampleRate, m_bufferSize);
    }
}
