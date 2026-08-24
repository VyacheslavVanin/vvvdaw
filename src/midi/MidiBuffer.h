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
    bool isCc() const { return (status & 0xF0) == 0xB0; }
    bool isPitchBend() const { return (status & 0xF0) == 0xE0; }
    bool isChannelPressure() const { return (status & 0xF0) == 0xD0; }
    bool isProgramChange() const { return (status & 0xF0) == 0xC0; }
    bool isPolyPressure() const { return (status & 0xF0) == 0xA0; }
    // Any channel voice message (note, CC, pitch bend, ...).
    bool isChannelVoice() const { return (status & 0x80) != 0 && (status & 0xF0) != 0xF0; }
    // True when the message carries a discrete value in data1 (CC, note,
    // pressure, program change) — excludes pitch bend's 14-bit pair.
    bool hasDiscreteValue() const {
        return isCc() || isNoteOn() || isNoteOff() || isChannelPressure()
            || isProgramChange() || isPolyPressure();
    }

    uint8_t channel() const { return status & 0x0F; }
};

using MidiBuffer = std::vector<MidiMessage>;
