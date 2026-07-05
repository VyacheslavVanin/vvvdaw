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
