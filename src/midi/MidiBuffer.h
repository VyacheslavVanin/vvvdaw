#pragma once
#include <cstdint>
#include <vector>

struct MidiMessage {
    int sampleOffset = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;

    bool isNoteOn() const { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

using MidiBuffer = std::vector<MidiMessage>;
