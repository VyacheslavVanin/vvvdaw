#pragma once
#include <QString>
#include <memory>
#include <vector>
#include <cstdint>
#include "MidiBuffer.h"
#include "MidiMessageRing.h"

enum class MidiTransportCommand : uint8_t {
    None = 0,
    Play,
    Stop,
    Record
};

// Which action is being learned from the MIDI input device.
enum class MidiLearnTarget : uint8_t {
    None = 0,
    Play,
    Record,
    Stop
};

// Mapping that turns incoming MIDI messages into transport commands. `type` is
// 0 (disabled), 1 (Control Change) or 2 (Note / other channel voice message).
// `channel` is the MIDI channel the commands must arrive on (0-15, or -1 for
// any channel). `kind` records the exact status high-nibble of the learned
// message (0xB0 CC, 0x90 note-on, 0x80 note-off, 0xA0 poly pressure, 0xC0
// program change, 0xD0 channel pressure); when 0 it is derived from `type`
// (1 -> 0xB0, 2 -> 0x90).
struct MidiTransportControls {
    int type = 1;
    int kind = 0;
    int channel = -1;
    int play = 110;
    int record = 111;
    int stop = 112;
};

class MidiInputManager {
public:
    MidiInputManager();
    ~MidiInputManager();

    struct Device {
        int id = -1;
        QString name;
    };

    static std::vector<Device> enumerateInputDevices();

    // Parse one complete MIDI message (first byte is the status byte, as
    // delivered by RtMidi) into a MidiMessage. Testable without hardware.
    static bool parseMessage(const std::vector<uint8_t>& bytes, MidiMessage& out);

    // At most one input device is active at a time: opening a new one closes
    // the previous. Safe to call while the audio stream is running.
    void open(int deviceId);
    void close(int deviceId);
    void closeAll();
    bool isActive() const;

    void setTransportControls(const MidiTransportControls& controls);

    // Live capture of the next incoming CC / note-on message for transport
    // learning. Only one target at a time; a captured message is consumed (not
    // delivered as a note or transport command).
    void setLearnTarget(MidiLearnTarget target);
    MidiLearnTarget learnTarget() const;
    // GUI thread: pop the captured learn message, filling `out` (type + value).
    bool popLearned(MidiTransportControls& out);

    // Audio-thread consumer: all note messages received since the last poll.
    bool hasPendingNotes() const;
    int pollNotes(MidiMessage* out, int maxCount);

    // GUI-thread consumer: transport commands received since the last poll.
    int pollTransport(MidiTransportCommand* out, int maxCount);

    // Testable routing helpers (no hardware needed).
    // Which transport command a message matches under the given controls.
    static MidiTransportCommand matchTransport(const MidiMessage& msg,
                                               const MidiTransportControls& controls);
    // True when a message is a candidate for transport learning (any channel
    // voice message that carries a note/control value in data1).
    static bool isLearnable(const MidiMessage& msg);

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};