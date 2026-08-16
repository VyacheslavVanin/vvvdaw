#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

// Convert a linear amplitude to decibels, clamped to [-60, 0] dB for metering.
inline float linearToDecibels(float linear) {
    if (linear <= 0.0f)
        return -60.0f;
    float db = 20.0f * std::log10(linear);
    if (db < -60.0f) db = -60.0f;
    if (db > 0.0f) db = 0.0f;
    return db;
}

// Inverse of linearToDecibels for the same clamped [-60, 0] dB range.
// -60 dB maps to true silence (0), 0 dB to unity (1).
inline float decibelsToLinear(float db) {
    if (db <= -60.0f)
        return 0.0f;
    if (db >= 0.0f)
        return 1.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Peak of an interleaved (stereo, L/R per frame) bus buffer.
inline float busBufferPeak(const float* interleaved, unsigned long frames) {
    float peak = 0.0f;
    for (unsigned long f = 0; f < frames; ++f) {
        float l = std::fabs(interleaved[f * 2]);
        float r = std::fabs(interleaved[f * 2 + 1]);
        peak = std::max(peak, std::max(l, r));
    }
    return peak;
}

// Compute the order in which buses must be processed so that every bus is
// mixed into its targets before any target is processed (a topological sort of
// the routing DAG). `targets[i]` lists the destinations bus i feeds: its main
// output (or < 0 / >= n for "to output device") plus every send target. Buses
// that feed nobody are roots and come first; any nodes left over (routing
// cycles) are appended unchanged so every bus still appears exactly once.
inline std::vector<int> computeBusProcessOrder(const std::vector<std::vector<int>>& targets,
                                               int busCount) {
    std::vector<int> inDegree(static_cast<size_t>(busCount), 0);
    for (int i = 0; i < busCount; ++i) {
        for (int t : targets[static_cast<size_t>(i)]) {
            if (t >= 0 && t < busCount && t != i)
                inDegree[static_cast<size_t>(t)]++;
        }
    }

    std::vector<int> order;
    order.reserve(static_cast<size_t>(busCount));
    std::vector<int> queue;
    for (int i = 0; i < busCount; ++i) {
        if (inDegree[static_cast<size_t>(i)] == 0)
            queue.push_back(i);
    }

    while (!queue.empty()) {
        int node = queue.back();
        queue.pop_back();
        order.push_back(node);

        for (int t : targets[static_cast<size_t>(node)]) {
            if (t >= 0 && t < busCount && t != node) {
                inDegree[static_cast<size_t>(t)]--;
                if (inDegree[static_cast<size_t>(t)] == 0)
                    queue.push_back(t);
            }
        }
    }

    if (static_cast<int>(order.size()) < busCount) {
        for (int i = 0; i < busCount; ++i)
            if (std::find(order.begin(), order.end(), i) == order.end())
                order.push_back(i);
    }
    return order;
}

// Which buses stay audible when at least one bus is soloed. `targets[i]` lists
// the destinations bus i feeds (main output + send targets) and `solo[i]`
// whether bus i is soloed. A bus passes through when it is:
//   - itself soloed,
//   - on a route from a soloed bus up to the output (an ancestor, following
//     the soloed bus's feed chain), so the soloed signal can reach the
//     speakers, or
//   - feeding a soloed bus (the soloed bus is reachable from it through the
//     main-output or send edges), so the soloed bus receives the sources that
//     contribute to it.
// Everything else is silenced. Returns a pass/no-pass flag per bus index.
inline std::vector<bool> computeBusSoloPassSet(const std::vector<std::vector<int>>& targets,
                                               const std::vector<bool>& solo,
                                               int busCount) {
    std::vector<bool> pass(static_cast<size_t>(busCount), false);

    // Soloed buses and every bus on their route up to the output (their forward
    // feed chain), so the soloed signal can actually reach the speakers.
    for (int i = 0; i < busCount; ++i) {
        if (!solo[static_cast<size_t>(i)]) continue;
        std::vector<bool> visited(static_cast<size_t>(busCount), false);
        std::vector<int> stack = { i };
        while (!stack.empty()) {
            int cur = stack.back();
            stack.pop_back();
            if (cur < 0 || cur >= busCount || visited[static_cast<size_t>(cur)])
                continue;
            visited[static_cast<size_t>(cur)] = true;
            pass[static_cast<size_t>(cur)] = true;
            for (int t : targets[static_cast<size_t>(cur)])
                if (t >= 0 && t < busCount)
                    stack.push_back(t);
        }
    }

    // Buses that (transitively) feed into a soloed bus: the soloed bus is
    // reachable from them through the main-output or send edges.
    for (int i = 0; i < busCount; ++i) {
        std::vector<bool> visited(static_cast<size_t>(busCount), false);
        std::vector<int> stack = { i };
        bool feedsSolo = false;
        while (!stack.empty() && !feedsSolo) {
            int cur = stack.back();
            stack.pop_back();
            if (cur < 0 || cur >= busCount || visited[static_cast<size_t>(cur)])
                continue;
            if (solo[static_cast<size_t>(cur)]) { feedsSolo = true; break; }
            visited[static_cast<size_t>(cur)] = true;
            for (int t : targets[static_cast<size_t>(cur)])
                if (t >= 0 && t < busCount)
                    stack.push_back(t);
        }
        if (feedsSolo)
            pass[static_cast<size_t>(i)] = true;
    }

    return pass;
}
