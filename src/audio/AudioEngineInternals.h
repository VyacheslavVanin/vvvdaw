#pragma once
// Small constants and helpers shared by the AudioEngine translation units
// (AudioEngine.cpp and its per-area split files). Inline so each TU that
// includes them gets its own copy without extra linkage.

#include <cstddef>
#include "model/Project.h"

namespace vvvdaw {
namespace audioengine {

constexpr size_t kInstrumentMidiReserve = 256;
constexpr float kDownbeatClickGain = 0.6f;
constexpr float kSilenceThreshold = 1e-5f;
constexpr float kMonoDownmix = 0.5f;

// Click envelope constants (5 ms click at 1 kHz with exponential decay).
constexpr int kClickLengthMs = 5;
constexpr double kClickDecayRate = 800.0;
constexpr double kClickFrequency = 1000.0;

inline bool anyTrackSolo(const Project* proj) {
    for (const auto& track : proj->tracks())
        if (track.isSolo()) return true;
    return false;
}

inline bool anyInstrumentSolo(const Project* proj) {
    for (const auto& inst : proj->instruments())
        if (inst.isSolo()) return true;
    return false;
}

inline bool anyBusSolo(const Project* proj) {
    for (const auto& bus : proj->buses())
        if (bus.isSolo()) return true;
    return false;
}

} // namespace audioengine
} // namespace vvvdaw