#pragma once
#include <memory>
#include <vector>
#include <QString>
#include <sndfile.h>
#include "core/Constants.h"

class AudioClip {
public:
    struct Peak { float min; float max; };
    // Coarse peak level (1 peak per 512 frames) for very zoomed-out views.
    static constexpr size_t PEAK_STEP_FRAMES = 512;
    // Fine peak level (1 peak per 16 frames) for the envelope at moderate zoom.
    static constexpr size_t FINE_PEAK_STEP_FRAMES = 16;

    AudioClip() = default;
    explicit AudioClip(const QString& filePath);
    AudioClip(std::vector<float>&& samples, int sampleRate, int channels);

    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath) const;
    bool saveToFile(const QString& filePath, int sampleRate) const;

    // Read `frameCount` interleaved frames starting at `startFrame` into `out`
    // (resized to frameCount*channels). Works for streaming and in-memory
    // clips; used to render zoomed-in views at sample resolution.
    bool readFrames(size_t startFrame, size_t frameCount,
                    std::vector<float>& out) const;

    const float* data() const { return m_samples.data(); }
    float* data() { return m_samples.data(); }
    size_t frameCount() const { return m_frameCount; }
    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    double durationSeconds() const;
    bool isValid() const { return m_frameCount > 0; }
    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    bool isStreaming() const { return m_streaming; }

    static void setStreamingThresholdFrames(size_t frames) { s_streamingThresholdFrames = frames; }
    static size_t streamingThresholdFrames() { return s_streamingThresholdFrames; }

    const std::vector<Peak>& peaks() const { return m_peaks; }
    size_t peaksPerFrame() const { return PEAK_STEP_FRAMES; }

    const std::vector<Peak>& finePeaks() const { return m_finePeaks; }
    size_t finePeaksPerFrame() const { return FINE_PEAK_STEP_FRAMES; }

    static constexpr size_t DEFAULT_STREAMING_THRESHOLD_FRAMES = 30 * vvvdaw::DefaultSampleRate;

private:
    void computePeaks();
    void computePeaksFromFile(SNDFILE* file, const SF_INFO& info);

    QString m_filePath;
    std::vector<float> m_samples;
    size_t m_frameCount = 0;
    int m_channels = 0;
    int m_sampleRate = 0;
    bool m_streaming = false;

    std::vector<Peak> m_peaks;
    std::vector<Peak> m_finePeaks;

    static size_t s_streamingThresholdFrames;
};
