#pragma once
#include <sndfile.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <QString>
#include "RingBuffer.h"

class AudioClip;
class Project;

struct PlaybackStream {
    AudioClip* clip = nullptr;
    SNDFILE* file = nullptr;
    SF_INFO info{};
    RingBuffer buffer;
    int64_t eventStartSample = 0;
    int64_t eventOffsetSample = 0;
    int64_t eventDurationSample = 0;
    int64_t endFrame = 0;
    bool readerFinished = false;
    bool finished = false;

    bool open(const QString& filePath, int64_t startFrame, int64_t endFrame);
    void close();
    void resetPosition(int64_t startFrame);
};

class StreamingManager {
public:
    StreamingManager();

    void start(Project* project, int64_t playPosition);
    void stop();
    void createStreams(Project* project, int64_t playPosition);

    // Audio callback: read data for a streamed event.
    // Returns true if data was available, false if stream not found or finished.
    // outFramesRead: number of complete frames available in scratch buffer.
    bool readEvent(const AudioClip* clip, int64_t eventStartSample, int64_t eventDurationSample,
                   float* scratch, size_t maxFrames, int ch, size_t& outFramesRead);

    void signalReset(int64_t newPos);
    void notifyReader();
    void closeAll();

private:
    void readerThreadFunc();

    std::vector<PlaybackStream> m_playbackStreams;
    std::mutex m_streamMutex;
    std::thread m_readerThread;
    std::mutex m_readerMutex;
    std::condition_variable m_readerCond;
    std::atomic<bool> m_readerRunning{false};
    std::atomic<bool> m_needStreamReset{false};
    std::atomic<int64_t> m_resetStreamPos{0};
    std::vector<float> m_scratch;
};
