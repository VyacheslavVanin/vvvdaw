#pragma once
#include <algorithm>
#include <utility>

// Compute per-channel gain factors from a stereo pan value [-1, 1].
// -1 = full left, 0 = center, +1 = full right.
inline std::pair<float, float> panGains(float pan) {
    return {
        std::min(1.0f, 1.0f - pan),
        std::min(1.0f, 1.0f + pan)
    };
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
// the interleaved stereo bus buffer, applying volume + pan gains.
inline void addSourceToBus(float* busBuf, const float* src, int srcCh,
                           unsigned long frames, float vol,
                           float leftGain, float rightGain, bool isMono) {
    if (isMono) {
        for (unsigned long f = 0; f < frames; ++f) {
            float s = src[f * srcCh] * vol;
            busBuf[f * 2]     += s * leftGain;
            busBuf[f * 2 + 1] += s * rightGain;
        }
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            float sL = src[f * srcCh];
            float sR = srcCh > 1 ? src[f * srcCh + 1] : sL;
            busBuf[f * 2]     += sL * vol * leftGain;
            busBuf[f * 2 + 1] += sR * vol * rightGain;
        }
    }
}

// Accumulate split (mono/stereo) track buffers into the interleaved stereo
// bus buffer, applying volume + pan gains.
inline void writeTrackToBus(float* busBuf, const float* trackL, const float* trackR,
                            unsigned long frames, float vol,
                            float leftGain, float rightGain, bool isMono) {
    if (isMono) {
        for (unsigned long f = 0; f < frames; ++f) {
            float s = trackL[f] * vol;
            busBuf[f * 2]     += s * leftGain;
            busBuf[f * 2 + 1] += s * rightGain;
        }
    } else {
        for (unsigned long f = 0; f < frames; ++f) {
            busBuf[f * 2]     += trackL[f] * vol * leftGain;
            busBuf[f * 2 + 1] += trackR[f] * vol * rightGain;
        }
    }
}
