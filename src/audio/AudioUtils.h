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

// Accumulate an interleaved source (srcCh channels per frame) directly into
// the interleaved stereo bus buffer, applying volume + pan.
inline void addSourceToBus(float* busBuf, const float* src, int srcCh,
                           unsigned long frames, float vol,
                           float pan, bool isMono) {
    if (isMono) {
        auto [leftGain, rightGain] = panGains(pan);
        for (unsigned long f = 0; f < frames; ++f) {
            float s = src[f * srcCh] * vol;
            busBuf[f * 2]     += s * leftGain;
            busBuf[f * 2 + 1] += s * rightGain;
        }
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            float sL = src[f * srcCh];
            float sR = srcCh > 1 ? src[f * srcCh + 1] : sL;
            float lo, ro;
            panStereo(sL, sR, pan, lo, ro);
            busBuf[f * 2]     += lo * vol;
            busBuf[f * 2 + 1] += ro * vol;
        }
    }
}

// Accumulate split (mono/stereo) track buffers into the interleaved stereo
// bus buffer, applying volume + pan.
inline void writeTrackToBus(float* busBuf, const float* trackL, const float* trackR,
                            unsigned long frames, float vol,
                            float pan, bool isMono) {
    if (isMono) {
        auto [leftGain, rightGain] = panGains(pan);
        for (unsigned long f = 0; f < frames; ++f) {
            float s = trackL[f] * vol;
            busBuf[f * 2]     += s * leftGain;
            busBuf[f * 2 + 1] += s * rightGain;
        }
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            float lo, ro;
            panStereo(trackL[f], trackR[f], pan, lo, ro);
            busBuf[f * 2]     += lo * vol;
            busBuf[f * 2 + 1] += ro * vol;
        }
    }
}
