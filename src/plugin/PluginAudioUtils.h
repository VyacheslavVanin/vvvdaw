#pragma once
#include <algorithm>
#include <cstring>

// Common buffer shuffling shared by the LV2 and VST3 audio backends.

// Bypass path: when `active` is false copy the input straight to the output.
// Returns true when the caller should skip processing.
inline bool bypassPassthrough(bool active, float** inputBuffers, float** outputBuffers,
                              int numSamples, int numChannels) {
    if (active)
        return false;
    if (outputBuffers && inputBuffers) {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy(outputBuffers[ch], inputBuffers[ch], numSamples * sizeof(float));
    }
    return true;
}

// Stereo -> mono fold for mono-input plugins: average the two channels.
inline void foldStereoToMono(float* dest, float** inputBuffers, int numSamples) {
    for (int s = 0; s < numSamples; ++s)
        dest[s] = (inputBuffers[0][s] + inputBuffers[1][s]) * 0.5f;
}

// Mono -> stereo duplicate so downstream mixing sees a centered signal.
inline void duplicateMonoToStereo(const float* src, float** outputBuffers, int numSamples) {
    if (!src)
        return;
    std::memcpy(outputBuffers[1], src, static_cast<size_t>(numSamples) * sizeof(float));
}

// Copies host channels into a bus that expects more channels, repeating the
// host channels cyclically (mono -> stereo, stereo -> quad, ...). Used when a
// mono track feeds a stereo plugin: a narrower layout is rejected by some
// plugins (JUCE's VST3 wrapper hands them a blank buffer).
inline void expandChannelsToBus(const float* const* src, int srcChannels,
                                float* const* dst, int dstChannels, int numSamples) {
    if (!src || !dst || srcChannels <= 0) return;
    for (int c = 0; c < dstChannels; ++c)
        std::memcpy(dst[c], src[c % srcChannels],
                    static_cast<size_t>(numSamples) * sizeof(float));
}

// Folds a plugin's output channels down to the host's count by averaging the
// channels assigned round-robin to each host channel (stereo -> mono etc.).
inline void reduceChannelsToHost(const float* const* src, int srcChannels,
                                 float* const* dst, int dstChannels, int numSamples) {
    if (!src || !dst || dstChannels <= 0) return;
    for (int h = 0; h < dstChannels; ++h) {
        std::fill(dst[h], dst[h] + numSamples, 0.0f);
        int count = 0;
        for (int c = h; c < srcChannels; c += dstChannels) {
            const float* in = src[c];
            for (int s = 0; s < numSamples; ++s) dst[h][s] += in[s];
            ++count;
        }
        if (count > 1) {
            const float scale = 1.0f / static_cast<float>(count);
            for (int s = 0; s < numSamples; ++s) dst[h][s] *= scale;
        }
    }
}
