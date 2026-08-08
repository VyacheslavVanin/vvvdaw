#include "AudioClip.h"
#include <sndfile.h>
#include <cstring>
#include <QDebug>

size_t AudioClip::s_streamingThresholdFrames = AudioClip::DEFAULT_STREAMING_THRESHOLD_FRAMES;

AudioClip::AudioClip(const QString& filePath) {
    loadFromFile(filePath);
}

AudioClip::AudioClip(std::vector<float>&& samples, int sampleRate, int channels)
    : m_samples(std::move(samples))
    , m_sampleRate(sampleRate)
    , m_channels(channels)
{
    m_frameCount = m_channels > 0 ? m_samples.size() / m_channels : 0;
    computePeaks();
}

bool AudioClip::loadFromFile(const QString& filePath) {
    SF_INFO info;
    std::memset(&info, 0, sizeof(info));

    SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_READ, &info);
    if (!file) {
        qWarning() << "Failed to open audio file:" << filePath << sf_strerror(nullptr);
        return false;
    }

    m_filePath = filePath;
    m_sampleRate = info.samplerate;
    m_channels = info.channels;
    m_frameCount = info.frames;

    size_t threshold = s_streamingThresholdFrames > 0 ? s_streamingThresholdFrames : DEFAULT_STREAMING_THRESHOLD_FRAMES;
    if (info.frames > threshold) {
        computePeaksFromFile(file, info);
        sf_close(file);
        m_streaming = true;
        m_samples.clear();
        return true;
    }

    m_samples.resize(m_frameCount * m_channels);
    sf_readf_float(file, m_samples.data(), m_frameCount);
    sf_close(file);

    computePeaks();
    return true;
}

bool AudioClip::saveToFile(const QString& filePath) const {
    return saveToFile(filePath, m_sampleRate);
}

bool AudioClip::saveToFile(const QString& filePath, int sampleRate) const {
    if (m_streaming) {
        qWarning() << "Cannot save streaming clip to file";
        return false;
    }
    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.samplerate = sampleRate > 0 ? sampleRate : m_sampleRate;
    info.channels = m_channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
    if (!file) {
        qWarning() << "Failed to write audio file:" << filePath << sf_strerror(nullptr);
        return false;
    }

    sf_writef_float(file, m_samples.data(), m_frameCount);
    sf_close(file);
    return true;
}

namespace {

// Number of peak slots for a given frame count.
size_t peakSlotCount(size_t frames) {
    return (frames + AudioClip::PEAK_STEP_FRAMES - 1) / AudioClip::PEAK_STEP_FRAMES;
}

// Peak absolute value of the first channel across `frames` interleaved frames.
float peakOfChannel(const float* data, size_t frames, int channels) {
    float maxAbs = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float s = std::abs(data[i * channels]);
        if (s > maxAbs) maxAbs = s;
    }
    return maxAbs;
}

} // namespace

void AudioClip::computePeaks() {
    m_peaks.clear();
    if (m_frameCount == 0 || m_channels == 0) return;

    m_peaks.reserve(peakSlotCount(m_frameCount));

    for (size_t f = 0; f < m_frameCount; f += PEAK_STEP_FRAMES) {
        size_t end = std::min(f + PEAK_STEP_FRAMES, m_frameCount);
        m_peaks.push_back({peakOfChannel(m_samples.data() + f * m_channels,
                                         end - f, m_channels)});
    }
}

void AudioClip::computePeaksFromFile(SNDFILE* file, const SF_INFO& info) {
    m_peaks.clear();
    if (info.frames == 0 || info.channels == 0) return;

    m_peaks.reserve(peakSlotCount(static_cast<size_t>(info.frames)));

    std::vector<float> buf(static_cast<size_t>(PEAK_STEP_FRAMES) * info.channels);

    for (sf_count_t f = 0; f < info.frames; f += PEAK_STEP_FRAMES) {
        sf_count_t toRead = std::min<sf_count_t>(PEAK_STEP_FRAMES, info.frames - f);
        sf_count_t read = sf_readf_float(file, buf.data(), toRead);
        m_peaks.push_back({peakOfChannel(buf.data(), static_cast<size_t>(read),
                                         info.channels)});
    }
}

double AudioClip::durationSeconds() const {
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / m_sampleRate;
}
