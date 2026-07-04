#pragma once
#include <memory>
#include <vector>
#include <QString>

class AudioClip {
public:
    AudioClip() = default;
    explicit AudioClip(const QString& filePath);
    AudioClip(std::vector<float>&& samples, int sampleRate, int channels);

    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath) const;
    bool saveToFile(const QString& filePath, int sampleRate) const;

    const float* data() const { return m_samples.data(); }
    float* data() { return m_samples.data(); }
    size_t frameCount() const { return m_frameCount; }
    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    double durationSeconds() const;
    bool isValid() const { return m_frameCount > 0; }
    const QString& filePath() const { return m_filePath; }

    bool isStreaming() const { return m_streaming; }

    static void setStreamingThresholdFrames(size_t frames) { s_streamingThresholdFrames = frames; }
    static size_t streamingThresholdFrames() { return s_streamingThresholdFrames; }

    std::shared_ptr<const std::vector<float>> sharedData() const { return m_sharedData; }

    static constexpr size_t DEFAULT_STREAMING_THRESHOLD_FRAMES = 30 * 48000;

private:
    QString m_filePath;
    std::vector<float> m_samples;
    std::shared_ptr<const std::vector<float>> m_sharedData;
    size_t m_frameCount = 0;
    int m_channels = 0;
    int m_sampleRate = 0;
    bool m_streaming = false;

    static size_t s_streamingThresholdFrames;
};
