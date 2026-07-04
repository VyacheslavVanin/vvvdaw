#include "AudioClip.h"
#include <sndfile.h>
#include <cstring>
#include <QDebug>

AudioClip::AudioClip(const QString& filePath) {
    loadFromFile(filePath);
}

AudioClip::AudioClip(std::vector<float>&& samples, int sampleRate, int channels)
    : m_samples(std::move(samples))
    , m_sampleRate(sampleRate)
    , m_channels(channels)
{
    m_frameCount = m_channels > 0 ? m_samples.size() / m_channels : 0;
    m_sharedData = std::make_shared<const std::vector<float>>(m_samples);
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

    m_samples.resize(m_frameCount * m_channels);
    sf_readf_float(file, m_samples.data(), m_frameCount);
    sf_close(file);

    m_sharedData = std::make_shared<const std::vector<float>>(m_samples);
    return true;
}

bool AudioClip::saveToFile(const QString& filePath) const {
    return saveToFile(filePath, m_sampleRate);
}

bool AudioClip::saveToFile(const QString& filePath, int sampleRate) const {
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

double AudioClip::durationSeconds() const {
    if (m_sampleRate == 0) return 0.0;
    return static_cast<double>(m_frameCount) / m_sampleRate;
}
