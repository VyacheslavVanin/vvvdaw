#pragma once
#include <cstdint>

struct MidiNote {
    int64_t id = 0;
    int pitch = 60;
    int velocity = 100;
    int64_t startTick = 0;
    int64_t durationTicks = 0;

    int64_t endTick() const { return startTick + durationTicks; }
};
