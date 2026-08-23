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
#include "AudioEngineInternals.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <QDebug>

using vvvdaw::TransportState;
using namespace vvvdaw::audioengine;

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

    MidiTransportControls controls;
    controls.type = settings.midiTransportControlType;
    controls.kind = settings.midiTransportKind;
    controls.channel = settings.midiTransportChannel;
    controls.play = settings.midiTransportPlayControl;
    controls.record = settings.midiTransportRecordControl;
    controls.stop = settings.midiTransportStopControl;
    m_midiInput.setTransportControls(controls);
    setMidiInputDevice(settings.midiInputDeviceId);
    return true;
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
    m_midiInput.closeAll();
    m_midiInputDeviceId = -1;
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
        // Release preview notes held from the MIDI keyboard (a held key must
        // not keep ringing after stop).
        cancelPreviewNotes();
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
    if (m_transportState.load(std::memory_order_acquire) != TransportState::Playing)
        return;
    clearStretchSlots();
    m_streamingManager.closeAll();
    auto* proj = m_project.load(std::memory_order_acquire);
    if (proj)
        m_streamingManager.createStreams(proj, pos);
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

    if (state == TransportState::Playing || state == TransportState::Recording) {
        int64_t pos = m_playPosition.load(std::memory_order_acquire);

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

        pollMidiInput(proj, pos, frameCount, state);
        processBusMixing(proj, output, frameCount, pos, outCh, input, inCh);

        if (state == TransportState::Recording)
            m_recordingManager.notifyWriter();

        m_playPosition.store(advancePlayhead(proj, pos, frameCount, state),
                             std::memory_order_release);
        return;
    }

    if ((state == TransportState::Stopped || state == TransportState::Paused)
        && ((input && inCh > 0) || m_previewCount.load(std::memory_order_acquire) > 0
            || m_midiInput.hasPendingNotes())) {
        auto* proj = m_project.load(std::memory_order_acquire);
        if (!proj) return;
        std::shared_lock projectLock(proj->mutex(), std::try_to_lock);
        if (!projectLock) return;
        int64_t pos = m_playPosition.load(std::memory_order_acquire);
        pollMidiInput(proj, pos, frameCount, state);
        processBusMixing(proj, output, frameCount, pos, outCh, input, inCh, true);
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


void AudioEngine::setProject(Project* project) {
    // Held preview notes target indices into the previous project; deliver
    // their note-offs (or drop them) before switching.
    cancelPreviewNotes();
    m_project.store(project, std::memory_order_release);
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

    for (auto& track : proj->tracks())
        deactivatePluginChain(const_cast<PluginChain&>(track.pluginChain()));
    for (auto& bus : proj->buses())
        deactivatePluginChain(bus.pluginChain());
    for (auto& inst : proj->instruments()) {
        if (inst.synth() && inst.synth()->isActive())
            inst.synth()->deactivate();
        deactivatePluginChain(inst.effects());
    }
}


void AudioEngine::activatePluginChain(PluginChain& chain) {
    for (int i = 0; i < chain.count(); ++i) {
        auto* plugin = chain.plugin(i);
        if (plugin && !plugin->isActive())
            plugin->activate(m_sampleRate, m_bufferSize);
    }
}


void AudioEngine::deactivatePluginChain(PluginChain& chain) {
    for (int i = 0; i < chain.count(); ++i) {
        auto* plugin = chain.plugin(i);
        if (plugin && plugin->isActive())
            plugin->deactivate();
    }
}
