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

bool AudioClip::readFrames(size_t startFrame, size_t frameCount,
                           std::vector<float>& out) const {
    out.clear();
    if (m_frameCount == 0 || m_channels == 0 || frameCount == 0)
        return false;
    if (startFrame >= m_frameCount)
        return false;

    size_t count = std::min(frameCount, m_frameCount - startFrame);

    if (!m_streaming) {
        out.resize(count * m_channels);
        std::memcpy(out.data(),
                    m_samples.data() + startFrame * m_channels,
                    count * m_channels * sizeof(float));
        return true;
    }

    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    SNDFILE* file = sf_open(m_filePath.toUtf8().constData(), SFM_READ, &info);
    if (!file) {
        qWarning() << "Failed to open audio file for reading:" << m_filePath
                   << sf_strerror(nullptr);
        return false;
    }

    out.resize(count * m_channels);
    sf_seek(file, static_cast<sf_count_t>(startFrame), SEEK_SET);
    sf_count_t read = sf_readf_float(file, out.data(), static_cast<sf_count_t>(count));
    sf_close(file);

    if (read <= 0) {
        out.clear();
        return false;
    }
    if (static_cast<size_t>(read) < count)
        out.resize(static_cast<size_t>(read) * m_channels);
    return true;
}

namespace {

// Number of peak slots for a given frame count and step.
size_t peakSlotCount(size_t frames, size_t step) {
    return (frames + step - 1) / step;
}

// Peak (signed min/max of the first channel) across `frames` interleaved frames.
AudioClip::Peak peakOfChannel(const float* data, size_t frames, int channels) {
    float min = 0.0f;
    float max = 0.0f;
    bool first = true;
    for (size_t i = 0; i < frames; ++i) {
        float s = data[i * channels];
        if (first) {
            min = max = s;
            first = false;
        } else {
            if (s < min) min = s;
            if (s > max) max = s;
        }
    }
    return {min, max};
}

// Build the coarse peak level by folding groups of fine peaks.
std::vector<AudioClip::Peak> coarseFromFine(const std::vector<AudioClip::Peak>& fine) {
    std::vector<AudioClip::Peak> coarse;
    const size_t per = AudioClip::PEAK_STEP_FRAMES / AudioClip::FINE_PEAK_STEP_FRAMES;
    coarse.reserve(fine.size() / per + 1);
    AudioClip::Peak acc{0.0f, 0.0f};
    size_t n = 0;
    for (const auto& p : fine) {
        if (n == 0) {
            acc = p;
        } else {
            if (p.min < acc.min) acc.min = p.min;
            if (p.max > acc.max) acc.max = p.max;
        }
        if (++n >= per) {
            coarse.push_back(acc);
            acc = {0.0f, 0.0f};
            n = 0;
        }
    }
    if (n > 0) coarse.push_back(acc);
    return coarse;
}

} // namespace

void AudioClip::computePeaks() {
    m_peaks.clear();
    m_finePeaks.clear();
    if (m_frameCount == 0 || m_channels == 0) return;

    m_finePeaks.reserve(peakSlotCount(m_frameCount, FINE_PEAK_STEP_FRAMES));

    for (size_t f = 0; f < m_frameCount; f += FINE_PEAK_STEP_FRAMES) {
        size_t end = std::min(f + FINE_PEAK_STEP_FRAMES, m_frameCount);
        m_finePeaks.push_back(peakOfChannel(m_samples.data() + f * m_channels,
                                            end - f, m_channels));
    }
    m_peaks = coarseFromFine(m_finePeaks);
}

void AudioClip::computePeaksFromFile(SNDFILE* file, const SF_INFO& info) {
    m_peaks.clear();
    m_finePeaks.clear();
    if (info.frames == 0 || info.channels == 0) return;

    m_finePeaks.reserve(peakSlotCount(static_cast<size_t>(info.frames),
                                      FINE_PEAK_STEP_FRAMES));

    std::vector<float> buf(static_cast<size_t>(FINE_PEAK_STEP_FRAMES) * info.channels);

    for (sf_count_t f = 0; f < info.frames; f += FINE_PEAK_STEP_FRAMES) {
        sf_count_t toRead = std::min<sf_count_t>(FINE_PEAK_STEP_FRAMES, info.frames - f);
        sf_count_t read = sf_readf_float(file, buf.data(), toRead);
        m_finePeaks.push_back(peakOfChannel(buf.data(), static_cast<size_t>(read),
                                            info.channels));
    }
    m_peaks = coarseFromFine(m_finePeaks);
}

double AudioClip::durationSeconds() const {
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / m_sampleRate;
}
