#include "TimeStretch.h"
#include <SoundTouch.h>
#include <algorithm>

namespace {

constexpr int kSequenceMs = 45;
constexpr int kSeekWindowMs = 18;
constexpr int kOverlapMs = 12;

}

TimeStretch::TimeStretch()
    : m_st(new soundtouch::SoundTouch()) {
    // Real-time friendly settings: shorter sequences than the SoundTouch
    // defaults reduce latency and CPU at a small quality cost.
    m_st->setSetting(SETTING_USE_QUICKSEEK, 0);
    m_st->setSetting(SETTING_SEQUENCE_MS, kSequenceMs);
    m_st->setSetting(SETTING_SEEKWINDOW_MS, kSeekWindowMs);
    m_st->setSetting(SETTING_OVERLAP_MS, kOverlapMs);
}

TimeStretch::~TimeStretch() {
    delete m_st;
}

void TimeStretch::setChannels(int channels) {
    if (channels <= 0)
        return;
    if (channels == m_ch && m_configured)
        return;
    m_ch = channels;
    reset();
    m_st->setChannels(static_cast<uint>(channels));
    m_configured = true;
}

void TimeStretch::setSampleRate(int sampleRate) {
    m_st->setSampleRate(static_cast<uint>(sampleRate));
}

void TimeStretch::reset() {
    m_st->clear();
    m_currentRate = 1.0;
}

void TimeStretch::push(const float* src, size_t frames) {
    if (frames == 0)
        return;
    m_st->putSamples(src, static_cast<uint>(frames));
}

size_t TimeStretch::pull(float* out, size_t outFrames, double rate) {
    if (outFrames == 0)
        return 0;
    if (rate <= 0.0)
        rate = 1.0;

    if (rate != m_currentRate) {
        // SoundTouch tempo: 1.0 = normal, <1 slower (stretch), >1 faster.
        // Pitch stays unchanged because only tempo is adjusted.
        m_st->setTempo(rate);
        m_currentRate = rate;
    }

    return static_cast<size_t>(m_st->receiveSamples(out, static_cast<uint>(outFrames)));
}
