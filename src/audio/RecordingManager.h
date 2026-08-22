#pragma once
#include <sndfile.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "RingBuffer.h"
#include "RecordingPeaks.h"

class Project;
class AudioClip;
class Track;

struct RecordingTrack {
    std::unique_ptr<RingBuffer> buffer;
    std::string filePath;
    SNDFILE* file = nullptr;
    SF_INFO info{};
    // Live waveform peaks of the audio captured so far (filled by the writer
    // thread, read by the GUI for the recording preview).
    std::unique_ptr<RecordingPeaks> peaks;
};

class RecordingManager {
public:
    RecordingManager();

    void start(Project* project, int sampleRate, int64_t playPosition);
    void stop(Project* project);

    bool isActive() const { return m_recordingActive.load(std::memory_order_acquire); }
    bool isRegionActive() const { return m_regionRecordingActive.load(std::memory_order_acquire); }
    int64_t recordStartSample() const { return m_recordStartSample; }

    void setRegionActive(bool active) { m_regionRecordingActive.store(active, std::memory_order_release); }

    // GUI thread: copy the live waveform peaks accumulated for a record-armed
    // track. Returns false if the track is not currently being recorded.
    bool copyRecordPeaks(int trackIndex, std::vector<AudioClip::Peak>& out,
                         int64_t& outRecordedFrames) const;

    // Called from audio callback (real-time, non-blocking)
    void processCapture(const float* input, unsigned long frameCount, int inCh);
    void notifyWriter();

    void setScratchSize(size_t frames);

private:
    void writerThreadFunc();
    bool processLoopRecordRegion(AudioClip& clip, const RecordingTrack& rt, Track& track,
                                  Project* proj, int64_t recordStartSample);

    mutable std::mutex m_recordingTracksMutex;
    std::unordered_map<int, RecordingTrack> m_recordingTracks;
    std::thread m_writerThread;
    std::mutex m_writerMutex;
    std::condition_variable m_writerCond;
    std::atomic<bool> m_writerRunning{false};
    std::atomic<bool> m_recordingActive{false};
    std::atomic<bool> m_regionRecordingActive{false};
    int64_t m_recordStartSample = 0;
    std::vector<float> m_scratch;
    int m_sampleRate = 48000;
};
