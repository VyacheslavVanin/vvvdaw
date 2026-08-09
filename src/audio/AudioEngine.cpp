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

namespace {

constexpr size_t kInstrumentMidiReserve = 256;
constexpr float kDownbeatClickGain = 0.6f;
constexpr float kSilenceThreshold = 1e-5f;
constexpr float kMonoDownmix = 0.5f;

// Click envelope constants (5 ms click at 1 kHz with exponential decay).
constexpr int kClickLengthMs = 5;
constexpr double kClickDecayRate = 800.0;
constexpr double kClickFrequency = 1000.0;

bool anyTrackSolo(const Project* proj) {
    for (const auto& track : proj->tracks())
        if (track.isSolo()) return true;
    return false;
}

bool anyInstrumentSolo(const Project* proj) {
    for (const auto& inst : proj->instruments())
        if (inst.isSolo()) return true;
    return false;
}

bool anyBusSolo(const Project* proj) {
    for (const auto& bus : proj->buses())
        if (bus.isSolo()) return true;
    return false;
}

} // namespace

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

    generateClickEnvelope();
    return true;
}

void AudioEngine::generateClickEnvelope() {
    m_clickEnvelopeSize = m_sampleRate * kClickLengthMs / 1000;
    m_clickEnvelope.resize(m_clickEnvelopeSize);
    for (int i = 0; i < m_clickEnvelopeSize; ++i) {
        double t = static_cast<double>(i) / m_sampleRate;
        double decay = std::exp(-t * kClickDecayRate);
        m_clickEnvelope[i] = static_cast<float>(std::sin(2.0 * M_PI * kClickFrequency * t) * decay);
    }
}

void AudioEngine::shutdown() {
    stopRecording();
    stopPlayback();
    cancelPreviewNotes();
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
        // Force the next playback block to flush notes still held by
        // instruments (their note-offs cannot be delivered while stopped).
        m_midiTransportActive = false;
        m_transportState.store(TransportState::Stopped, std::memory_order_release);
        return;
    }

    if (state == TransportState::Paused) {
        if (m_recordingManager.isActive())
            stopRecording();
        if (prev == TransportState::Playing || prev == TransportState::Recording)
            stopPlayback();
        // Any interruption (pause or stop) clears sounding notes: the next
        // playback block flushes notes still held by instruments. Only notes
        // whose onset the playhead reaches will sound.
        m_midiTransportActive = false;
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
        && ((input && inCh > 0) || m_previewCount.load(std::memory_order_acquire) > 0)) {
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

    {
        std::lock_guard<std::mutex> lock(m_meterMutex);
        m_busMeters.clear();
        for (int i = 0; i < busCount; ++i)
            m_busMeters.emplace_back();
    }

    std::vector<int> inDegree(busCount, 0);
    for (int i = 0; i < busCount; ++i) {
        int parent = proj->buses()[i].outputBusIndex();
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

        int parent = proj->buses()[node].outputBusIndex();
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

void AudioEngine::sendNoteOn(int destIndex, bool toInstrument, uint8_t channel,
                             uint8_t pitch, uint8_t velocity, int sampleOffset) {
    if (toInstrument) {
        if (destIndex >= 0 && destIndex < static_cast<int>(m_instrumentMidi.size())) {
            MidiMessage m;
            m.sampleOffset = sampleOffset;
            m.status = static_cast<uint8_t>(0x90 | channel);
            m.data1 = pitch;
            m.data2 = velocity;
            m_instrumentMidi[destIndex].push_back(m);
        }
    } else if (destIndex >= 0) {
        m_midiOutput.send(destIndex, static_cast<uint8_t>(0x90 | channel), pitch, velocity);
    }
}

void AudioEngine::sendNoteOff(int destIndex, bool toInstrument, uint8_t channel,
                              uint8_t pitch, int sampleOffset) {
    if (toInstrument) {
        if (destIndex >= 0 && destIndex < static_cast<int>(m_instrumentMidi.size())) {
            MidiMessage m;
            m.sampleOffset = sampleOffset;
            m.status = static_cast<uint8_t>(0x80 | channel);
            m.data1 = pitch;
            m.data2 = 0;
            m_instrumentMidi[destIndex].push_back(m);
        }
    } else if (destIndex >= 0) {
        m_midiOutput.send(destIndex, static_cast<uint8_t>(0x80 | channel), pitch, 0);
    }
}

void AudioEngine::sendActiveNoteOff(const ActiveMidiNote& note, int sampleOffset) {
    sendNoteOff(note.destIndex, note.toInstrument, note.channel, note.pitch, sampleOffset);
}

void AudioEngine::flushActiveMidiNotes() {
    for (const auto& an : m_activeMidiNotes)
        sendActiveNoteOff(an, 0);
    m_activeMidiNotes.clear();
}

void AudioEngine::scheduleMidiTracks(Project* proj, unsigned long frameCount, int64_t pos) {
    bool anySolo = anyTrackSolo(proj);

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.type() != Track::Type::Midi) { ++trackIndex; continue; }

        int instIdx = track.instrumentIndex();
        bool toInstrument = (instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size()));
        bool targetMuted = toInstrument && proj->instruments()[instIdx].isMuted();

        auto flushTrackNotes = [&](int tIndex) {
            for (auto it = m_activeMidiNotes.begin(); it != m_activeMidiNotes.end();) {
                if (it->trackIndex == tIndex) {
                    sendActiveNoteOff(*it, 0);
                    it = m_activeMidiNotes.erase(it);
                } else {
                    ++it;
                }
            }
        };

        // Muted track, muted target instrument, or track skipped by solo: release
        // notes still held by the destination so they do not keep ringing (and do
        // not resume sounding after unmute).
        if (track.isMuted() || targetMuted || (anySolo && !track.isSolo())) {
            flushTrackNotes(trackIndex);
            ++trackIndex;
            continue;
        }

        int deviceId = track.midiOutputDeviceId();
        int destIdx = toInstrument ? instIdx : deviceId;
        uint8_t channel = static_cast<uint8_t>(trackIndex % 16);

        for (const auto& event : track.midiEvents()) {
            auto clip = event.activeClip();
            if (!clip) continue;

            int64_t eventEnd = event.startSample() + event.durationSample();

            // The event has ended: cut off every note from it that is still
            // sounding. Notes drawn past the clip boundary have their own
            // note-off beyond the event end, so without this they would ring
            // forever once the event stops being processed.
            if (pos >= eventEnd) {
                for (auto it = m_activeMidiNotes.begin(); it != m_activeMidiNotes.end();) {
                    if (it->trackIndex == trackIndex && it->eventId == event.id()) {
                        sendActiveNoteOff(*it, 0);
                        it = m_activeMidiNotes.erase(it);
                    } else {
                        ++it;
                    }
                }
                continue;
            }
            if (pos + static_cast<int64_t>(frameCount) <= event.startSample())
                continue;

            int64_t offsetTicks = proj->samplesToTicks(event.offsetSample());
            for (const auto& note : clip->notes()) {
                int64_t noteStart = event.startSample()
                    + proj->ticksToSamples(note.startTick - offsetTicks);
                int64_t noteEnd = event.startSample()
                    + proj->ticksToSamples(note.endTick() - offsetTicks);

                // Note onset at/after the event's end: never sound it (its
                // onset would coincide with the cut point and it could never
                // receive a note-off).
                if (noteStart >= eventEnd)
                    continue;
                // Clamp the note to the event boundary so the note-off is
                // scheduled while the event is still processed.
                if (noteEnd > eventEnd)
                    noteEnd = eventEnd;

                // Allow noteEnd == pos: a note ending exactly on a block
                // boundary must still receive its note-off (at offset 0 of
                // this block), otherwise it would ring forever.
                if (noteEnd < pos || noteStart >= pos + static_cast<int64_t>(frameCount))
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
                    // A note whose onset lies before the current playback
                    // position and which is not already sounding was missed
                    // (seek / stop / loop wrap). Do not catch it up: it should
                    // not play, and no note-off is needed since the synth never
                    // received its note-on.
                    if (noteStart < pos)
                        continue;
                    int off = static_cast<int>(noteStart - pos);
                    if (off < static_cast<int>(frameCount)) {
                        sendNoteOn(toInstrument ? instIdx : deviceId, toInstrument,
                                   channel, static_cast<uint8_t>(note.pitch),
                                   static_cast<uint8_t>(note.velocity), off);
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
                    ActiveMidiNote an;
                    an.trackIndex = trackIndex;
                    an.eventId = event.id();
                    an.noteId = note.id;
                    an.destIndex = destIdx;
                    an.toInstrument = toInstrument;
                    an.channel = channel;
                    an.pitch = static_cast<uint8_t>(note.pitch);
                    sendActiveNoteOff(an, off);
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

    bool anySolo = anyInstrumentSolo(proj);

    int busCount = static_cast<int>(proj->buses().size());
    for (int i = 0; i < instCount; ++i) {
        auto& inst = proj->instruments()[i];
        if (inst.isMuted() || !inst.synth()) {
            // Deliver pending note-offs (queued when the instrument or its
            // feeding track got muted) so held notes are released instead of
            // resuming after unmute. Output is discarded.
            if (inst.synth() && !m_instrumentMidi[i].empty()) {
                std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.begin() + frameCount, 0.0f);
                std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.begin() + frameCount, 0.0f);
                float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
                float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
                inst.synth()->process(inBufs, outBufs, frameCount, 2, &m_instrumentMidi[i]);
            }
            continue;
        }
        if (anySolo && !inst.isSolo()) continue;

        int busIdx = inst.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;

        // Multi-channel routing: run the synth (and effects) with one buffer
        // per output channel, then accumulate each mapped channel into its bus.
        if (inst.isMultiChannel()) {
            int outCh = inst.synth()->audioOutputChannels();
            if (outCh < 1) outCh = 1;
            ensureMultiScratch(outCh);
            for (int c = 0; c < outCh; ++c)
                std::fill(m_multiScratch[c].begin(), m_multiScratch[c].begin() + frameCount, 0.0f);

            inst.synth()->process(m_multiInBufs.data(), m_multiOutBufs.data(),
                                  frameCount, outCh, &m_instrumentMidi[i]);
            if (inst.effects().count() > 0)
                inst.effects().process(m_multiInBufs.data(), m_multiOutBufs.data(),
                                       frameCount, outCh);

            const auto& routes = inst.channelRoutes();
            int nRoutes = std::min<int>(outCh, static_cast<int>(routes.size()));
            for (int c = 0; c < nRoutes; ++c) {
                int bIdx = routes[c].busIndex;
                if (bIdx < 0 || bIdx >= busCount) bIdx = 0;
                routeMonoToBus(m_busBuffers[bIdx].data(), m_multiScratch[c].data(),
                               frameCount, inst.volume());
            }
            continue;
        }

        float* busBuf = m_busBuffers[busIdx].data();

        std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.begin() + frameCount, 0.0f);
        std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.begin() + frameCount, 0.0f);
        float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
        float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };

        inst.synth()->process(inBufs, outBufs, frameCount, 2, &m_instrumentMidi[i]);
        if (inst.effects().count() > 0)
            inst.effects().process(inBufs, outBufs, frameCount, 2);

        for (unsigned long f = 0; f < frameCount; ++f) {
            float lo, ro;
            panStereo(m_instrumentScratchL[f], m_instrumentScratchR[f], inst.pan(), lo, ro);
            busBuf[f * 2]     += lo * inst.volume();
            busBuf[f * 2 + 1] += ro * inst.volume();
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

    ensureInstrumentMidiBuffers(static_cast<int>(proj->instruments().size()));
    for (auto& b : m_instrumentMidi)
        b.clear();

    bool firstActiveBlock = !m_midiTransportActive && !monitoringOnly;
    bool midiJumped = m_midiTransportActive &&
        (pos < m_lastMidiPos || pos - m_lastMidiPos > static_cast<int64_t>(frameCount) * 2);
    if ((firstActiveBlock && !m_activeMidiNotes.empty()) || midiJumped)
        flushActiveMidiNotes();
    // Mark active transport once playback blocks start so the discontinuity
    // flush only runs on real jumps. The flag is reset to false on any
    // explicit Stop or Pause (see setTransportState), which makes the next
    // playback block flush notes still held by instruments.
    if (!monitoringOnly)
        m_midiTransportActive = true;
    m_lastMidiPos = pos;

    mixTracksToBuses(proj, frameCount, pos, input, inCh, monitoringOnly);

    if (!monitoringOnly) {
        scheduleMidiTracks(proj, frameCount, pos);
        injectPreviewMidi();
        processInstruments(proj, frameCount);
    } else if (m_previewCount.load(std::memory_order_acquire) > 0) {
        injectPreviewMidi();
        processInstruments(proj, frameCount);
    }

    if (m_metronomeEnabled && !monitoringOnly && busCount > 1)
        generateClick(proj, m_busBuffers[1].data(), frameCount, pos, outCh);

    processBusChainsAndRoute(proj, output, frameCount, outCh);
}

void AudioEngine::mixTracksToBuses(Project* proj, unsigned long frameCount, int64_t pos,
                                   const float* input, int inCh, bool monitoringOnly) {
    int busCount = static_cast<int>(proj->buses().size());
    bool hasTrackSolo = anyTrackSolo(proj);

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.isMuted()) { ++trackIndex; continue; }
        if (hasTrackSolo && !track.isSolo()) { ++trackIndex; continue; }

        int busIdx = track.outputBusIndex();
        if (busIdx < 0 || busIdx >= busCount) busIdx = 0;

        float trackVol = track.volume();
        float pan = track.pan();

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
                               pan, isMono);
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
                                       framesAvail, trackVol, pan, isMono);
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
                            pan, isMono);
        }
        ++trackIndex;
    }
}

void AudioEngine::ensureInstrumentMidiBuffers(int instCount) {
    if (static_cast<int>(m_instrumentMidi.size()) != instCount) {
        m_instrumentMidi.resize(static_cast<size_t>(instCount));
        for (auto& b : m_instrumentMidi)
            b.reserve(kInstrumentMidiReserve);
        m_instrumentCount = instCount;
    }
}

void AudioEngine::ensureMultiScratch(int channels) {
    if (channels <= 0) return;
    if (static_cast<int>(m_multiScratch.size()) < channels) {
        m_multiScratch.resize(static_cast<size_t>(channels));
        m_multiInBufs.resize(static_cast<size_t>(channels));
        m_multiOutBufs.resize(static_cast<size_t>(channels));
    }
    for (int c = 0; c < channels; ++c) {
        if (m_multiScratch[c].size() < static_cast<size_t>(m_bufferSize))
            m_multiScratch[c].resize(static_cast<size_t>(m_bufferSize), 0.0f);
        m_multiInBufs[c] = m_multiScratch[c].data();
        m_multiOutBufs[c] = m_multiScratch[c].data();
    }
}

void AudioEngine::processBusChainsAndRoute(Project* proj, float* output,
                                           unsigned long frameCount, int outCh) {
    int busCount = static_cast<int>(proj->buses().size());
    bool hasBusSolo = anyBusSolo(proj);

    // Under solo, a bus stays audible when it is soloed, is on the route from
    // a soloed bus to the output, or feeds a soloed bus; everything else is
    // silenced. Skipping only the non-soloed buses (as before) made the
    // ancestors of a soloed bus silent too, killing all output.
    std::vector<bool> soloPass;
    if (hasBusSolo) {
        std::vector<int> outputTo(static_cast<size_t>(busCount));
        std::vector<bool> soloFlags(static_cast<size_t>(busCount), false);
        for (int i = 0; i < busCount; ++i) {
            outputTo[i] = proj->buses()[i].outputBusIndex();
            soloFlags[i] = proj->buses()[i].isSolo();
        }
        soloPass = computeBusSoloPassSet(outputTo, soloFlags, busCount);
    }

    for (int idx : m_busProcessOrder) {
        const auto& bus = proj->buses()[idx];

        if (bus.pluginChain().count() > 0) {
            float* buf = m_busBuffers[idx].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                m_busDeinterleaveL[f] = buf[f * 2];
                m_busDeinterleaveR[f] = buf[f * 2 + 1];
            }
            float* inBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            float* outBufs[2] = { m_busDeinterleaveL.data(), m_busDeinterleaveR.data() };
            bus.pluginChain().process(inBufs, outBufs, frameCount, 2);
            for (unsigned long f = 0; f < frameCount; ++f) {
                buf[f * 2]     = m_busDeinterleaveL[f];
                buf[f * 2 + 1] = m_busDeinterleaveR[f];
            }
        }

        if (bus.isMuted()) {
            setBusMeter(idx, 0.0f, false);
            continue;
        }
        if (hasBusSolo && !soloPass[static_cast<size_t>(idx)]) {
            setBusMeter(idx, 0.0f, false);
            continue;
        }

        float bVol = bus.volume();
        float peak = busBufferPeak(m_busBuffers[idx].data(), frameCount) * bVol;
        setBusMeter(idx, peak, peak >= 1.0f);

        int parentIdx = bus.outputBusIndex();
        bool routeToOutput = (parentIdx < 0 || parentIdx >= busCount);

        if (routeToOutput) {
            float* buf = m_busBuffers[idx].data();
            if (outCh >= 2) {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    float lo, ro;
                    panStereo(buf[f * 2], buf[f * 2 + 1], bus.pan(), lo, ro);
                    output[f * 2]     += lo * bVol;
                    output[f * 2 + 1] += ro * bVol;
                }
            } else {
                for (unsigned long f = 0; f < frameCount; ++f) {
                    output[f] += (buf[f * 2] + buf[f * 2 + 1]) * kMonoDownmix * bVol;
                }
            }
        } else {
            float* srcBuf = m_busBuffers[idx].data();
            float* dstBuf = m_busBuffers[parentIdx].data();
            for (unsigned long f = 0; f < frameCount; ++f) {
                float lo, ro;
                panStereo(srcBuf[f * 2], srcBuf[f * 2 + 1], bus.pan(), lo, ro);
                dstBuf[f * 2]     += lo * bVol;
                dstBuf[f * 2 + 1] += ro * bVol;
            }
        }
    }
}

int64_t AudioEngine::advancePlayhead(Project* proj, int64_t pos, unsigned long frameCount,
                                      TransportState state) {
    int64_t newPos = pos + frameCount;

    if (proj->hasLoop()) {
        int64_t loopEnd = proj->loopEnd();
        if (newPos >= loopEnd) {
            // Wrap exactly to the loop start. Keeping the block overshoot
            // (loopStart + excess) would permanently skip the first `excess`
            // samples of the loop on every pass, so a note starting at
            // loopStart would never re-trigger after the first pass.
            newPos = proj->loopStart();
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
    // Release notes still held by instrument synths right now (under a project
    // write lock the audio thread drops blocks, so this is race-free). Doing it
    // at stop time means a later play after seeking the playhead starts clean:
    // no leftover note and no release-tail blip from the previous session.
    releaseInstruments();
}

void AudioEngine::releaseInstruments() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;
    if (m_activeMidiNotes.empty()) return;

    int instCount = static_cast<int>(proj->instruments().size());
    if (instCount == 0 || m_instrumentScratchL.empty()) {
        m_activeMidiNotes.clear();
        return;
    }

    std::unique_lock projectLock(proj->mutex());

    ensureInstrumentMidiBuffers(instCount);
    for (int i = 0; i < instCount; ++i)
        m_instrumentMidi[i].clear();

    // Build one note-off per active note targeting each instrument.
    for (const auto& an : m_activeMidiNotes)
        sendNoteOff(an.destIndex, true, an.channel, an.pitch);

    // Render the release tails to silence so nothing is left ringing when
    // playback later resumes. Only run for instruments that actually held notes.
    const int maxReleaseBlocks = std::max(1, m_sampleRate / std::max(1, m_bufferSize));
    float* inBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
    float* outBufs[2] = { m_instrumentScratchL.data(), m_instrumentScratchR.data() };
    MidiBuffer emptyBuf;

    for (int i = 0; i < instCount; ++i) {
        auto& inst = proj->instruments()[i];
        if (m_instrumentMidi[i].empty()) continue;
        if (!inst.synth() || !inst.synth()->isActive()) continue;

        const bool multi = inst.isMultiChannel();
        int outCh = multi ? inst.synth()->audioOutputChannels() : 2;
        if (outCh < 1) outCh = 1;
        if (multi)
            ensureMultiScratch(outCh);

        float** inB = multi ? m_multiInBufs.data() : inBufs;
        float** outB = multi ? m_multiOutBufs.data() : outBufs;

        for (int b = 0; b < maxReleaseBlocks; ++b) {
            if (multi) {
                for (int c = 0; c < outCh; ++c)
                    std::fill(m_multiScratch[c].begin(), m_multiScratch[c].end(), 0.0f);
            } else {
                std::fill(m_instrumentScratchL.begin(), m_instrumentScratchL.end(), 0.0f);
                std::fill(m_instrumentScratchR.begin(), m_instrumentScratchR.end(), 0.0f);
            }
            const MidiBuffer* buf = (b == 0) ? &m_instrumentMidi[i] : &emptyBuf;
            inst.synth()->process(inB, outB, m_bufferSize, outCh, buf);
            if (inst.effects().count() > 0)
                inst.effects().process(inB, outB, m_bufferSize, outCh);

            float peak = 0.0f;
            for (int c = 0; c < outCh; ++c) {
                const float* chan = multi ? m_multiScratch[c].data()
                                          : (c == 0 ? m_instrumentScratchL.data()
                                                    : m_instrumentScratchR.data());
                for (int s = 0; s < m_bufferSize; ++s)
                    peak = std::max(peak, std::abs(chan[s]));
            }
            if (peak < kSilenceThreshold)
                break;
        }
    }

    m_activeMidiNotes.clear();
}

void AudioEngine::setBusMeter(int busIndex, float peak, bool clipped) {
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return;
    m_busMeters[busIndex].peak.store(peak, std::memory_order_relaxed);
    if (clipped)
        m_busMeters[busIndex].clipped.store(true, std::memory_order_relaxed);
}

float AudioEngine::busMeterPeak(int busIndex) const {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return 0.0f;
    return m_busMeters[busIndex].peak.load(std::memory_order_relaxed);
}

bool AudioEngine::busMeterClipping(int busIndex) const {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return false;
    return m_busMeters[busIndex].clipped.load(std::memory_order_relaxed);
}

void AudioEngine::clearBusMeterClip(int busIndex) {
    std::lock_guard<std::mutex> lock(m_meterMutex);
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busMeters.size()))
        return;
    m_busMeters[busIndex].clipped.store(false, std::memory_order_relaxed);
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

void AudioEngine::setProject(Project* project) {
    // Held preview notes target indices into the previous project; deliver
    // their note-offs (or drop them) before switching.
    cancelPreviewNotes();
    m_project.store(project, std::memory_order_release);
}

void AudioEngine::previewNoteOn(int trackIndex, int pitch, int velocity) {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj || trackIndex < 0 || trackIndex >= static_cast<int>(proj->tracks().size()))
        return;
    const auto& track = proj->tracks()[trackIndex];
    if (track.type() != Track::Type::Midi) return;

    int instIdx = track.instrumentIndex();
    bool toInstrument = instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size());
    int target = toInstrument ? instIdx : track.midiOutputDeviceId();
    if (!toInstrument && target < 0) return;

    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (n.trackIndex == trackIndex && n.pitch == static_cast<uint8_t>(pitch)) {
            n.velocity = static_cast<uint8_t>(velocity);
            return;
        }
    }
    PreviewHeldNote n;
    n.trackIndex = trackIndex;
    n.channel = static_cast<uint8_t>(trackIndex % 16);
    n.pitch = static_cast<uint8_t>(pitch);
    n.velocity = static_cast<uint8_t>(velocity);
    n.target = target;
    n.toInstrument = toInstrument;
    m_previewHeld.push_back(n);
    ++m_previewCount;
}

void AudioEngine::previewNoteOff(int trackIndex, int pitch) {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (n.trackIndex == trackIndex && n.pitch == static_cast<uint8_t>(pitch)
            && !n.offPending) {
            n.offPending = true;
            return;
        }
    }
}

void AudioEngine::cancelPreviewNotes(int trackIndex) {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (trackIndex < 0 || n.trackIndex == trackIndex)
            n.offPending = true;
    }
}

void AudioEngine::injectPreviewMidi() {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    if (m_previewHeld.empty()) return;

    for (auto& n : m_previewHeld) {
        if (n.offPending) {
            sendNoteOff(n.target, n.toInstrument, n.channel, n.pitch);
            continue;
        }
        if (n.noteOnSent) continue;

        if (n.target >= 0) {
            sendNoteOn(n.target, n.toInstrument, n.channel, n.pitch, n.velocity);
            n.noteOnSent = true;
        } else {
            n.offPending = true; // nowhere to play it: drop
        }
    }

    m_previewHeld.erase(
        std::remove_if(m_previewHeld.begin(), m_previewHeld.end(),
                       [](const PreviewHeldNote& n) { return n.offPending; }),
        m_previewHeld.end());
    if (m_previewHeld.empty())
        m_previewCount.store(0);
}

void AudioEngine::renderClickSample(float* outL, float* outR, int64_t samplePos,
                                   double samplesPerBeat, double samplesPerBar, float gain) {
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
            clickSample *= kDownbeatClickGain;
        *outL += clickSample * gain;
        *outR += clickSample * gain;
        m_clickPlayhead++;
    } else {
        m_clickPlayhead = -1;
    }
}

void AudioEngine::generateClick(Project* proj, float* buffer, unsigned long frameCount,
                                 int64_t pos, int outCh) {
    double samplesPerBeat = proj->samplesPerBeat();
    double samplesPerBar = proj->samplesPerBar();
    if (samplesPerBeat <= 0) return;

    for (unsigned long f = 0; f < frameCount; ++f)
        renderClickSample(&buffer[f * 2], &buffer[f * 2 + 1], pos + f,
                          samplesPerBeat, samplesPerBar, 1.0f);
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
        metroVol = proj->buses()[1].volume();

    for (unsigned long f = 0; f < frameCount; ++f) {
        if (m_precountPosition >= m_precountTotalSamples) break;

        renderClickSample(&output[f * 2], &output[f * 2 + 1], m_precountPosition,
                          samplesPerBeat, samplesPerBar, metroVol);
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
        activatePluginChain(bus.pluginChain());
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
        for (int i = 0; i < bus.pluginChain().count(); ++i) {
            auto* plugin = bus.pluginChain().plugin(i);
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
