#pragma once
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
