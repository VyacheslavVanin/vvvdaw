#pragma once
#include <portaudio.h>
#include <sndfile.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <QString>
#include <thread>
#include <unordered_map>
#include <vector>
#include "core/Settings.h"

class Project;
class AudioClip;

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

// Ring buffer: background reader thread → audio callback (SPSC)
struct PlaybackBuffer {
    std::vector<float> data;
    std::atomic<size_t> writePos{0};
    std::atomic<size_t> readPos{0};

    explicit PlaybackBuffer(size_t capacity = 32768);
    PlaybackBuffer(PlaybackBuffer&& other) noexcept;
    PlaybackBuffer& operator=(PlaybackBuffer&& other) noexcept;
    size_t write(const float* samples, size_t count);
    size_t read(float* dest, size_t maxCount);
    size_t available() const;
    size_t capacity() const { return data.size(); }
    void reset();
};

struct PlaybackStream {
    AudioClip* clip = nullptr;
    SNDFILE* file = nullptr;
    SF_INFO info{};
    PlaybackBuffer buffer;
    int64_t eventStartSample = 0;
    int64_t eventOffsetSample = 0;
    int64_t eventDurationSample = 0;
    int64_t endFrame = 0;    // last readable frame in file (exclusive)
    bool readerFinished = false; // reader has reached endFrame
    bool finished = false;      // all data consumed by callback

    bool open(const QString& filePath, int64_t startFrame, int64_t endFrame);
    void close();
    void resetPosition(int64_t startFrame);
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
    void setPlayPosition(int64_t pos);

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

    void startPlayback();
    void stopPlayback();
    void createPlaybackStreams();
    void readerThreadFunc();

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
    std::atomic<bool> m_regionRecordingActive{false};
    int64_t m_recordStartSample = 0;

    // Streaming playback
    std::vector<PlaybackStream> m_playbackStreams;
    std::mutex m_streamMutex;
    std::thread m_readerThread;
    std::mutex m_readerMutex;
    std::condition_variable m_readerCond;
    std::atomic<bool> m_readerRunning{false};
    std::atomic<bool> m_needStreamReset{false};
    std::atomic<int64_t> m_resetStreamPos{0};
};
