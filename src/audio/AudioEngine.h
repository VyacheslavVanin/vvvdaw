#pragma once
#include <portaudio.h>
#include <sndfile.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "core/Settings.h"

class Project;

enum class TransportState : uint8_t {
    Stopped,
    Playing,
    Paused,
    Recording
};

struct RecordingBuffer {
    std::vector<float> data;
    std::atomic<size_t> writePos{0};
    std::atomic<size_t> readPos{0};

    explicit RecordingBuffer(size_t capacity = 48000 * 10);
    size_t write(const float* samples, size_t count);
    size_t read(float* dest, size_t maxCount);
    size_t available() const;
    size_t capacity() const { return data.size(); }
};

struct RecordingTrack {
    std::unique_ptr<RecordingBuffer> buffer;
    std::string filePath;
    SNDFILE* file = nullptr;
    SF_INFO info{};
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(const Settings& settings);
    void shutdown();

    bool startStream();
    bool stopStream();
    bool restartStream(const Settings& settings);

    static std::vector<DeviceInfo> enumerateInputDevices();
    static std::vector<DeviceInfo> enumerateOutputDevices();

    void setProject(Project* project) { m_project = project; }

    void setTransportState(TransportState state);
    TransportState transportState() const;

    int64_t playPosition() const { return m_playPosition.load(std::memory_order_acquire); }
    void setPlayPosition(int64_t pos) { m_playPosition.store(pos, std::memory_order_release); }

    int sampleRate() const { return m_sampleRate; }
    int bufferSize() const { return m_bufferSize; }
    bool isActive() const { return m_stream != nullptr; }

    void startRecording();
    void stopRecording();

private:
    static int audioCallback(const void* input, void* output,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData);

    void processAudio(const float* input, float* output, unsigned long frameCount);
    void writerThreadFunc();

    PaStream* m_stream = nullptr;
    Project* m_project = nullptr;
    int m_sampleRate = 48000;
    int m_bufferSize = 512;
    int m_inputChannels = 0;
    int m_outputChannels = 2;

    std::atomic<TransportState> m_transportState{TransportState::Stopped};
    std::atomic<int64_t> m_playPosition{0};

    // Recording
    std::vector<float> m_stereoScratch;
    std::unordered_map<int, RecordingTrack> m_recordingTracks;
    std::thread m_writerThread;
    std::mutex m_writerMutex;
    std::condition_variable m_writerCond;
    std::atomic<bool> m_writerRunning{false};
    bool m_recordingActive = false;
};
