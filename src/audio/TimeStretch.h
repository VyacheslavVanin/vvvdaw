#pragma once
#include <cstddef>
#include <cstdint>

namespace soundtouch {
class SoundTouch;
}

// Real-time WSOLA pitch-preserving time-stretch, backed by SoundTouch
// (libsoundtouch). The stretch factor is implied by the playback rate
// `rate = sourceFrames / timelineDuration` (source frames consumed per output
// frame): rate < 1 slows the audio down (stretch), rate > 1 speeds it up
// (compress). Pitch is always preserved.
//
// Source audio (interleaved, `channels()` streams) is pushed at its native rate
// and pulled as a time-stretched signal.
class TimeStretch {
public:
    // Pre-roll in source frames pushed ahead on a fresh stream so that the first
    // pull() can return a full output block immediately (covers SoundTouch's
    // internal processing latency, ~60 ms at 48 kHz).
    static constexpr size_t kWindowSize = 4800;

    TimeStretch();
    ~TimeStretch();

    void reset();
    void setChannels(int channels);
    void setSampleRate(int sampleRate);

    int channels() const { return m_ch; }

    // Feed native-rate source frames (interleaved).
    void push(const float* src, size_t frames);

    // Produce up to outFrames stretched output frames (interleaved).
    // Returns the number of frames actually produced (0 if more input is needed).
    size_t pull(float* out, size_t outFrames, double rate);

private:
    soundtouch::SoundTouch* m_st;
    int m_ch = 2;
    double m_currentRate = 1.0;
    bool m_configured = false;
};
