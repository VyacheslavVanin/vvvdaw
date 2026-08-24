#pragma once
#include <cstdint>

// A point-in-time MIDI control event (continuous controller / pitch bend)
// stored in a MidiClip. Value is applied when the playhead reaches
// `startTick` and holds until the next event of the same kind+number.
struct MidiControlEvent {
    enum class Kind : uint8_t {
        ControlChange = 0, // number = CC number, value = 0..127
        PitchBend,         // number = 0, value = 0..16383 (center 8192)
    };

    int64_t id = 0;
    Kind kind = Kind::ControlChange;
    uint8_t number = 0;
    int value = 0;
    int64_t startTick = 0;
};