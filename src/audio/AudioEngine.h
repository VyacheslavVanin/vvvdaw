#pragma once
#include <portaudio.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include "core/Constants.h"
#include "core/Settings.h"
#include "DeviceInfo.h"
#include "RecordingManager.h"
#include "StreamingManager.h"
#include "TimeStretch.h"
#include "midi/MidiBuffer.h"
#include "midi/MidiInputManager.h"
#include "midi/MidiOutputManager.h"
#include "MidiRecorder.h"

class Project;
class Track;
class PluginChain;
class AudioEvent;
class AudioClip;

class TestMidi;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(const Settings& settings);
    void shutdown();

    bool startStream();
    bool stopStream();
    bool restartStream(const Settings& settings);

    static std::vector<DeviceInfo> enumerateInputDevices() { return enumerateDevices(true); }
    static std::vector<DeviceInfo> enumerateOutputDevices() { return enumerateDevices(false); }
    static std::vector<DeviceInfo> enumerateDevices(bool input);

    void setProject(Project* project);

    void setTransportState(vvvdaw::TransportState state);
    vvvdaw::TransportState transportState() const;

    int64_t playPosition() const { return m_playPosition.load(std::memory_order_acquire); }
    void setPlayPosition(int64_t pos);

    void setMetronomeEnabled(bool enabled) { m_metronomeEnabled = enabled; }
    void setPrecountEnabled(bool enabled) { m_precountEnabled = enabled; }
    bool metronomeEnabled() const { return m_metronomeEnabled; }
    bool precountEnabled() const { return m_precountEnabled; }

    int sampleRate() const { return m_sampleRate; }
    int bufferSize() const { return m_bufferSize; }
    bool isActive() const { return m_stream != nullptr; }

    void startRecording();
    void stopRecording();

    void activateAllPlugins();
    void deactivateAllPlugins();
    void activatePluginChain(PluginChain& chain);
    void deactivatePluginChain(PluginChain& chain);

    // Post-fader output metering for bus `busIndex` (linear amplitude peak and
    // a latched clip flag). Safe to call from the GUI thread while the audio
    // thread runs; clearBusMeterClip() resets the latched clip after display.
    float busMeterPeak(int busIndex) const;
    bool busMeterClipping(int busIndex) const;
    void clearBusMeterClip(int busIndex);

    void refreshMidiOutputs();
    void panicMidi();

    // Piano-roll key preview: note-ons are injected into the instrument / MIDI
    // device the track routes to and sustained until the matching note-off
    // (or cancelPreviewNotes) is delivered on the audio thread.
    void previewNoteOn(int trackIndex, int pitch, int velocity);
    void previewNoteOff(int trackIndex, int pitch);
    void cancelPreviewNotes(int trackIndex = -1);

    static std::vector<MidiOutputManager::Device> enumerateMidiOutputDevices() {
        return MidiOutputManager::enumerateOutputDevices();
    }

    static std::vector<MidiInputManager::Device> enumerateMidiInputDevices() {
        return MidiInputManager::enumerateInputDevices();
    }

    // MIDI keyboard preview target (the track of the active piano roll).
    void setMidiPreviewTrack(int trackIndex);
    int midiPreviewTrack() const { return m_midiPreviewTrack.load(std::memory_order_acquire); }

    void setMidiTransportControls(const MidiTransportControls& controls);

    // Open/close the MIDI input device live (safe while the stream runs). Used
    // by the settings dialog to learn transport mappings on the chosen device.
    void setMidiInputDevice(int deviceId);

    // Transport-mapping learning from the MIDI input device.
    void setMidiLearnTarget(MidiLearnTarget target);
    MidiLearnTarget midiLearnTarget() const;
    bool popLearnedMidiControl(MidiTransportControls& out);

    // GUI-thread consumer of MIDI transport commands.
    std::vector<MidiTransportCommand> takeMidiTransportCommands();

    // Capture/playback hook for MIDI recording (GUI thread drives pump()).
    MidiRecorder& midiRecorder() { return m_midiRecorder; }
    int64_t midiRecordStartSample() const { return m_recordingManager.recordStartSample(); }

    // Access to the live recording state and waveform peaks (GUI thread).
    RecordingManager& recordingManager() { return m_recordingManager; }

private:
    friend class TestMidi;

    static int audioCallback(const void* input, void* output,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData);

    void processAudio(const float* input, float* output, unsigned long frameCount);

    void processBusMixing(Project* proj, float* output, unsigned long frameCount,
                          int64_t pos, int outCh, const float* input, int inCh,
                          bool monitoringOnly = false);
    void rebuildBusGraph(Project* proj);
    // True when any bus's output routing differs from the last rebuilt graph.
    // Lets the audio thread refresh the process order when a bus is re-routed
    // without its count changing (e.g. adding a bus and routing an existing
    // one into it), which otherwise leaves the topological order stale.
    bool busRoutingChanged(const Project* proj) const;
    // Audio-thread helper: update the per-bus meter peak and latch the clip
    // flag (index bounds-checked; resize races are handled by m_meterMutex).
    void setBusMeter(int busIndex, float peak, bool clipped);
    int64_t advancePlayhead(Project* proj, int64_t pos, unsigned long frameCount,
                            vvvdaw::TransportState state);

    void generateClick(Project* proj, float* buffer, unsigned long frameCount,
                       int64_t pos, int outCh);
    void processPrecounting(Project* proj, float* output, unsigned long frameCount, int outCh);
    void startPrecount();

    // Reads one event's audio for the block starting at `pos` into `out`
    // (interleaved), applying pitch-preserving time-stretch when the event's
    // source length differs from its timeline duration. Returns frames produced.
    size_t readEventBlock(const AudioEvent& event, int trackIndex, int64_t pos,
                          float* out, size_t outFrames);
    void clearStretchSlots();

    void scheduleMidiTracks(Project* proj, unsigned long frameCount, int64_t pos);
    void processInstruments(Project* proj, unsigned long frameCount);
    void flushActiveMidiNotes();
    void releaseInstruments();
    void injectPreviewMidi();
    // Consume MIDI keyboard input for this block: feed preview notes into the
    // active piano-roll track and capture notes while recording.
    void pollMidiInput(Project* proj, int64_t pos, unsigned long frameCount,
                       vvvdaw::TransportState state);

    // Mix each audible track into its target bus buffer (monitoring, event
    // playback and track plugin chains).
    void mixTracksToBuses(Project* proj, unsigned long frameCount, int64_t pos,
                          const float* input, int inCh, bool monitoringOnly);
    // Run bus plugin chains and route each bus into its parent or the output.
    void processBusChainsAndRoute(Project* proj, float* output,
                                  unsigned long frameCount, int outCh);

    void ensureInstrumentMidiBuffers(int instCount);
    // Resize the multi-channel instrument scratch pool so at least `channels`
    // deinterleaved buffers (each m_bufferSize floats) are available.
    void ensureMultiScratch(int channels);
    void sendNoteOn(int destIndex, bool toInstrument, uint8_t channel,
                    uint8_t pitch, uint8_t velocity, int sampleOffset = 0);
    void sendNoteOff(int destIndex, bool toInstrument, uint8_t channel,
                     uint8_t pitch, int sampleOffset = 0);
    // Shared backend for sendNoteOn/sendNoteOff: delivers a MIDI message to the
    // instrument buffer or the external device.
    void queueMidiEvent(int destIndex, bool toInstrument, uint8_t status,
                        uint8_t pitch, uint8_t velocity, int sampleOffset);

    void generateClickEnvelope();
    // Advances the metronome click envelope for one sample position and adds
    // the (gain-scaled) click to the interleaved stereo sample pair.
    void renderClickSample(float* outL, float* outR, int64_t samplePos,
                           double samplesPerBeat, double samplesPerBar, float gain);

    void startPlayback();
    void stopPlayback();

    PaStream* m_stream = nullptr;
    std::atomic<Project*> m_project{nullptr};
    int m_sampleRate = 48000;
    int m_bufferSize = 512;
    int m_inputChannels = 0;
    int m_outputChannels = 2;

    std::atomic<vvvdaw::TransportState> m_transportState{vvvdaw::TransportState::Stopped};
    std::atomic<int64_t> m_playPosition{0};

    std::vector<float> m_stereoScratch;
    std::vector<float> m_trackScratch;
    std::vector<float> m_sourceScratch;
    std::vector<float> m_busDeinterleaveL;
    std::vector<float> m_busDeinterleaveR;
    std::vector<std::vector<float>> m_busBuffers;
    std::vector<int> m_busProcessOrder;
    // Routing snapshot (per-bus destination list: main output + send targets)
    // captured when the bus graph was last rebuilt; used by busRoutingChanged()
    // to detect re-routes.
    std::vector<std::vector<int>> m_busOutputs;
    int m_busCount = 0;
    // Per-bus post-fader output metering (written on the audio thread, read on
    // the GUI thread). Guarded by m_meterMutex only around the resize. Deque is
    // used because std::atomic members are neither copyable nor movable, which
    // std::vector would require.
    struct BusMeterState {
        std::atomic<float> peak{0.0f};
        std::atomic<bool> clipped{false};
    };
    mutable std::mutex m_meterMutex;
    std::deque<BusMeterState> m_busMeters;

    std::vector<MidiBuffer> m_instrumentMidi;
    int m_instrumentCount = -1;
    std::vector<float> m_instrumentScratchL;
    std::vector<float> m_instrumentScratchR;
    // Multi-channel instrument output scratch: [channel][sample], plus the
    // pointer arrays handed to PluginInstance::process (in-place).
    std::vector<std::vector<float>> m_multiScratch;
    std::vector<float*> m_multiInBufs;
    std::vector<float*> m_multiOutBufs;
    MidiOutputManager m_midiOutput;
    std::set<int> m_openMidiDevices;
    MidiInputManager m_midiInput;
    MidiRecorder m_midiRecorder;
    std::atomic<int> m_midiPreviewTrack{-1};
    int m_midiInputDeviceId = -1;

    // Held piano-roll preview notes (GUI thread queues them, audio thread
    // injects note-ons once and delivers note-offs).
    struct PreviewHeldNote {
        int trackIndex = -1;
        uint8_t channel = 0;
        uint8_t pitch = 0;
        uint8_t velocity = 100;
        int target = -1;
        bool toInstrument = false;
        bool noteOnSent = false;
        bool offPending = false;
    };
    std::mutex m_previewMutex;
    std::vector<PreviewHeldNote> m_previewHeld;
    std::atomic<int> m_previewCount{0};

    struct ActiveMidiNote {
        int trackIndex = 0;
        int64_t eventId = 0;
        int64_t noteId = 0;
        int destIndex = 0;
        bool toInstrument = false;
        uint8_t channel = 0;
        uint8_t pitch = 0;
    };
    void sendActiveNoteOff(const ActiveMidiNote& note, int sampleOffset);
    std::vector<ActiveMidiNote> m_activeMidiNotes;
    int64_t m_lastMidiPos = 0;
    bool m_midiTransportActive = false;

    RecordingManager m_recordingManager;
    StreamingManager m_streamingManager;

    struct StretchSlot {
        TimeStretch ts;
        const AudioClip* clip = nullptr;
        int64_t offset = 0;
        int64_t srcFrames = 0;
        int64_t duration = 0;
        int64_t srcCursor = 0;
    };
    std::unordered_map<int64_t, StretchSlot> m_stretchSlots;

    bool m_metronomeEnabled = false;
    bool m_precountEnabled = false;
    std::vector<float> m_clickEnvelope;
    int m_clickEnvelopeSize = 0;
    int m_clickPlayhead = -1;
    bool m_clickIsDownbeat = false;

    int64_t m_precountPosition = 0;
    int64_t m_precountTotalSamples = 0;
    int64_t m_precountStartPlayhead = 0;
};
