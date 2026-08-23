#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include "model/AudioClip.h"

// Live waveform peaks for a recording in progress. The writer thread appends
// one min/max peak per `AudioClip::FINE_PEAK_STEP_FRAMES` window while it
// flushes captured audio to disk; the GUI thread reads a snapshot for the
// growing recording preview. Safe for a single writer + single reader.
class RecordingPeaks {
public:
    void addSamples(const float* samples, size_t frames, int channels) {
        if (!samples || frames == 0 || channels <= 0)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t step = AudioClip::FINE_PEAK_STEP_FRAMES;
        for (size_t f = 0; f < frames; ++f) {
            float v = samples[f * channels];
            if (m_slotFrames == 0) {
                m_slotMin = v;
                m_slotMax = v;
            } else {
                if (v < m_slotMin) m_slotMin = v;
                if (v > m_slotMax) m_slotMax = v;
            }
            if (++m_slotFrames >= step) {
                m_peaks.push_back({m_slotMin, m_slotMax});
                m_slotMin = 0.0f;
                m_slotMax = 0.0f;
                m_slotFrames = 0;
            }
        }
        m_recordedFrames += static_cast<int64_t>(frames);
    }

    // Push the trailing partial window so the whole recording is represented.
    void flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_slotFrames > 0) {
            m_peaks.push_back({m_slotMin, m_slotMax});
            m_slotMin = 0.0f;
            m_slotMax = 0.0f;
            m_slotFrames = 0;
        }
    }

    std::vector<AudioClip::Peak> snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_peaks;
    }

    int64_t recordedFrames() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_recordedFrames;
    }

    size_t framesPerPeak() const { return AudioClip::FINE_PEAK_STEP_FRAMES; }

private:
    mutable std::mutex m_mutex;
    std::vector<AudioClip::Peak> m_peaks;
    float m_slotMin = 0.0f;
    float m_slotMax = 0.0f;
    size_t m_slotFrames = 0;
    int64_t m_recordedFrames = 0;
};