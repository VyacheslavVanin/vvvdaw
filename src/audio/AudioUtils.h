#pragma once
#include <algorithm>
#include <cmath>
#include <utility>

// Compute per-channel gain factors from a stereo pan value [-1, 1].
// -1 = full left, 0 = center, +1 = full right.
inline std::pair<float, float> panGains(float pan) {
    return {
        std::min(1.0f, 1.0f - pan),
        std::min(1.0f, 1.0f + pan)
    };
}

// Stereo pan with constant-power fold: a signal present in only one channel
// still transfers across the field (unlike a pure balance). Identity at
// pan == 0; at full left/right the signal folds to the corresponding mono
// side at -3 dB per original channel.
inline void panStereo(float l, float r, float pan, float& lo, float& ro) {
    const float k = 0.7071067811865476f; // 1/sqrt(2)
    float f = std::fabs(pan);
    if (pan >= 0.0f) {
        lo = l * (1.0f - f);
        ro = r * (1.0f - f) + (l + r) * f * k;
    } else {
        lo = l * (1.0f - f) + (l + r) * f * k;
        ro = r * (1.0f - f);
    }
}

// Accumulate an interleaved source (srcCh channels per frame) into split
// mono/stereo track buffers. For a mono track only channel 0 is read;
// for a stereo track the right channel duplicates the left when srcCh == 1.
inline void addSourceToTrack(float* trackL, float* trackR, const float* src,
                             int srcCh, unsigned long frames, bool isMono) {
    if (isMono) {
        for (unsigned long f = 0; f < frames; ++f)
            trackL[f] += src[f * srcCh];
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            float sL = src[f * srcCh];
            float sR = srcCh > 1 ? src[f * srcCh + 1] : sL;
            trackL[f] += sL;
            trackR[f] += sR;
        }
    }
}

// Shared bus-accumulation core: per frame the GetFrame callback returns the
// (left, right) pair from the source (interleaved buffer or split track
// buffers). Applies volume + pan and accumulates into the stereo bus buffer.
template <typename GetFrame>
inline void accumulateToBus(float* busBuf, unsigned long frames, float vol,
                            float pan, bool isMono, GetFrame getFrame) {
    if (isMono) {
        auto [leftGain, rightGain] = panGains(pan);
        for (unsigned long f = 0; f < frames; ++f) {
            float s = getFrame(f).first * vol;
            busBuf[f * 2]     += s * leftGain;
            busBuf[f * 2 + 1] += s * rightGain;
        }
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            auto [sL, sR] = getFrame(f);
            float lo, ro;
            panStereo(sL, sR, pan, lo, ro);
            busBuf[f * 2]     += lo * vol;
            busBuf[f * 2 + 1] += ro * vol;
        }
    }
}

// Accumulate an interleaved source (srcCh channels per frame) directly into
// the interleaved stereo bus buffer, applying volume + pan.
inline void addSourceToBus(float* busBuf, const float* src, int srcCh,
                           unsigned long frames, float vol,
                           float pan, bool isMono) {
    accumulateToBus(busBuf, frames, vol, pan, isMono, [&](unsigned long f) {
        float sL = src[f * srcCh];
        return std::pair<float, float>{ sL, srcCh > 1 ? src[f * srcCh + 1] : sL };
    });
}

// Accumulate split (mono/stereo) track buffers into the interleaved stereo
// bus buffer, applying volume + pan.
inline void writeTrackToBus(float* busBuf, const float* trackL, const float* trackR,
                            unsigned long frames, float vol,
                            float pan, bool isMono) {
    accumulateToBus(busBuf, frames, vol, pan, isMono, [&](unsigned long f) {
        return std::pair<float, float>{ trackL[f], trackR[f] };
    });
}

// Accumulate one mono instrument output channel into a (stereo, interleaved)
// bus buffer. The channel is placed centered (equal L and R gain) so multi-
// channel instrument outputs land on the bus field without hard panning.
inline void routeMonoToBus(float* busBuf, const float* src, unsigned long frames,
                           float vol) {
    for (unsigned long f = 0; f < frames; ++f) {
        float s = src[f] * vol;
        busBuf[f * 2]     += s;
        busBuf[f * 2 + 1] += s;
    }
}
