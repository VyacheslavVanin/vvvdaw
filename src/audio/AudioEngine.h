#pragma once
#include <portaudio.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "core/Constants.h"
#include "core/Settings.h"
#include "DeviceInfo.h"
#include "RecordingManager.h"
#include "StreamingManager.h"

class Project;
class Track;
class PluginChain;

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

    void setProject(Project* project) { m_project.store(project, std::memory_order_release); }

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
    void activatePluginChain(PluginChain& chain);

private:
    static int audioCallback(const void* input, void* output,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData);

    void processAudio(const float* input, float* output, unsigned long frameCount);

    void mixPlayback(Project* proj, float* output, unsigned long frameCount,
                     int64_t pos, int outCh);
    void processBusMixing(Project* proj, float* output, unsigned long frameCount,
                          int64_t pos, int outCh);
    void rebuildBusGraph(Project* proj);
    int64_t advancePlayhead(Project* proj, int64_t pos, unsigned long frameCount,
                            vvvdaw::TransportState state);

    void generateClick(Project* proj, float* buffer, unsigned long frameCount,
                       int64_t pos, int outCh);
    void processPrecounting(Project* proj, float* output, unsigned long frameCount, int outCh);
    void startPrecount();

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
    std::vector<float> m_busDeinterleaveL;
    std::vector<float> m_busDeinterleaveR;
    std::vector<std::vector<float>> m_busBuffers;
    std::vector<int> m_busProcessOrder;
    int m_busCount = 0;

    RecordingManager m_recordingManager;
    StreamingManager m_streamingManager;

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
