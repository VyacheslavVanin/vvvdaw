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

// Mapping that turns incoming MIDI messages into transport commands. `type` is
// 0 (disabled), 1 (Control Change) or 2 (Note On).
struct MidiTransportControls {
    int type = 1;
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
    // the previous. Must be called while the audio stream is stopped.
    void open(int deviceId);
    void close(int deviceId);
    void closeAll();
    bool isActive() const;

    void setTransportControls(const MidiTransportControls& controls);

    // Audio-thread consumer: all note messages received since the last poll.
    bool hasPendingNotes() const;
    int pollNotes(MidiMessage* out, int maxCount);

    // GUI-thread consumer: transport commands received since the last poll.
    int pollTransport(MidiTransportCommand* out, int maxCount);

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};