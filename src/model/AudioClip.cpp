#include "AudioClip.h"
#include <sndfile.h>
#include <algorithm>
#include <cmath>
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

// Forward-only source sampler: either a pointer into an in-memory buffer or a
// single-open sequential reader over a file. Streaming sources (long WAVs,
// MP3s, ...) are decoded in one pass instead of seeking per block, which keeps
// lossy formats free of seek-boundary glitches.
class SequentialSourceReader {
public:
    ~SequentialSourceReader() { close(); }

    bool open(const QString& path, int ch) {
        SF_INFO info;
        std::memset(&info, 0, sizeof(info));
        m_file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
        if (!m_file) return false;
        m_ch = ch;
        return true;
    }

    void useMemory(const float* data, int ch) {
        m_mem = data;
        m_ch = ch;
    }

    void close() {
        if (m_file) {
            sf_close(m_file);
            m_file = nullptr;
        }
    }

    // Make sure frame `frame` is loaded. Call before frameAt(); it may grow or
    // compact the window, so any previously returned pointer becomes invalid.
    void prefetch(int64_t frame) {
        if (m_mem)
            return;
        while (!m_eof && m_start + static_cast<int64_t>(validFrames()) <= frame) {
            if (m_off >= kChunkFrames)
                compact();
            readChunk();
        }
    }

    // Release frames before `frame` so the window stays bounded on long files.
    // Frames already loaded but not yet returned are dropped; the file cursor
    // (which only moves forward) is unaffected.
    void discardBefore(int64_t frame) {
        if (m_mem || frame <= m_start)
            return;
        const int64_t end = m_start + static_cast<int64_t>(validFrames());
        if (frame >= end) {
            m_start = end;
            m_off = m_win.size() / static_cast<size_t>(m_ch);
        } else {
            m_off += static_cast<size_t>(frame - m_start);
            m_start = frame;
        }
        if (m_off >= kChunkFrames)
            compact();
    }

    // Interleaved frame `frame`, or nullptr past the end. Does not modify the
    // window, so pointers stay valid as long as no prefetch()/discard follows.
    const float* frameAt(int64_t frame) const {
        if (m_mem)
            return m_mem + frame * static_cast<int64_t>(m_ch);
        if (frame < m_start)
            return nullptr;
        const size_t idx = m_off + static_cast<size_t>(frame - m_start);
        if (idx >= m_win.size() / static_cast<size_t>(m_ch))
            return nullptr;
        return m_win.data() + idx * static_cast<size_t>(m_ch);
    }

private:
    size_t validFrames() const {
        return m_win.size() / static_cast<size_t>(m_ch) - m_off;
    }

    // Drop the consumed prefix of the window (m_start already points at
    // m_win[m_off], so it is unchanged).
    void compact() {
        m_win.erase(m_win.begin(),
                    m_win.begin() + static_cast<long>(m_off * static_cast<size_t>(m_ch)));
        m_off = 0;
    }

    void readChunk() {
        const size_t base = m_win.size();
        m_win.resize(base + kChunkFrames * static_cast<size_t>(m_ch));
        const sf_count_t got = sf_readf_float(m_file, m_win.data() + base,
                                              kChunkFrames);
        if (got > 0)
            m_win.resize(base + static_cast<size_t>(got) * static_cast<size_t>(m_ch));
        else {
            m_win.resize(base);
            m_eof = true;
        }
    }

    static constexpr size_t kChunkFrames = 1 << 16;

    SNDFILE* m_file = nullptr;
    const float* m_mem = nullptr;
    int m_ch = 1;
    std::vector<float> m_win;
    size_t m_off = 0;      // first valid frame within m_win
    int64_t m_start = 0;   // global source frame of m_win[m_off]
    bool m_eof = false;
};

// Linear-interpolate one output block from `src` and write it. Returns false on
// a read or write failure.
bool resampleWriteBlock(SequentialSourceReader& src, int64_t frameCount, int ch,
                        SNDFILE* file, int64_t outStart, int64_t outCount,
                        double ratio, std::vector<float>& out) {
    out.resize(static_cast<size_t>(outCount) * static_cast<size_t>(ch));
    for (int64_t i = 0; i < outCount; ++i) {
        double srcPos = static_cast<double>(outStart + i) / ratio;
        int64_t i0 = static_cast<int64_t>(srcPos);
        double frac = srcPos - static_cast<double>(i0);
        if (i0 >= frameCount) { i0 = frameCount - 1; frac = 0.0; }
        const int64_t i1 = std::min(i0 + 1, frameCount - 1);
        // Drop consumed frames first, then load up to i1 and take both pointers
        // (frameAt never mutates the window, so they stay valid).
        src.discardBefore(i0);
        src.prefetch(i1);
        const float* a = src.frameAt(i0);
        const float* b = src.frameAt(i1);
        if (!a) return false;
        if (!b) b = a;
        for (int c = 0; c < ch; ++c) {
            out[static_cast<size_t>(i * ch + c)] =
                static_cast<float>(a[c] + (b[c] - a[c]) * frac);
        }
    }
    return sf_writef_float(file, out.data(),
                           static_cast<sf_count_t>(outCount)) == outCount;
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

// Valid inputs for an offline resample request.
bool canResampleFrames(size_t frameCount, int channels, int sampleRate,
                       int targetSampleRate) {
    return frameCount > 0 && channels > 0 && sampleRate > 0 && targetSampleRate > 0;
}

} // namespace

bool AudioClip::saveResampledToFile(const QString& filePath, int targetSampleRate) const {
    if (!canResampleFrames(m_frameCount, m_channels, m_sampleRate, targetSampleRate))
        return false;

    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.samplerate = targetSampleRate;
    info.channels = m_channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
    if (!file) {
        qWarning() << "Failed to write resampled audio file:" << filePath
                   << sf_strerror(nullptr);
        return false;
    }

    // Open the source once: streaming clips are decoded forward in a single
    // pass, in-memory clips expose their sample buffer directly.
    SequentialSourceReader src;
    if (isStreaming()) {
        if (!src.open(m_filePath, m_channels)) {
            qWarning() << "Failed to open audio file for resampling:" << m_filePath
                       << sf_strerror(nullptr);
            sf_close(file);
            return false;
        }
    } else {
        src.useMemory(m_samples.data(), m_channels);
    }

    const double ratio = static_cast<double>(targetSampleRate) / m_sampleRate;
    const int64_t outFrames = static_cast<int64_t>(
        std::llround(static_cast<double>(m_frameCount) * ratio));
    const int ch = m_channels;
    const int64_t frameCount = static_cast<int64_t>(m_frameCount);
    constexpr int64_t kBlockFrames = 4096;

    std::vector<float> outBuf;
    bool ok = true;
    for (int64_t outStart = 0; outStart < outFrames && ok; outStart += kBlockFrames) {
        const int64_t outCount = std::min(kBlockFrames, outFrames - outStart);
        ok = resampleWriteBlock(src, frameCount, ch, file, outStart, outCount,
                                ratio, outBuf);
    }

    sf_close(file);
    return ok;
}

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
