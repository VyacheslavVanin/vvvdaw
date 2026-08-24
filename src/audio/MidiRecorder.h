#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "midi/MidiBuffer.h"
#include "midi/MidiMessageRing.h"

class Project;
class MidiEvent;
class MidiClip;

struct TimedMidiMessage {
    int64_t sample = 0;
    MidiMessage msg;
};

// Captures MIDI input while recording and writes it into record-armed MIDI
// tracks. `captureNote()` is called from the audio thread (real-time safe: a
// lock-free push). All model mutation happens on the GUI thread in `pump()`,
// which MainWindow drives from its timer, so the audio thread never touches
// the project model.
class MidiRecorder {
public:
    MidiRecorder();

    // GUI thread: which piano-roll event to record into per track
    // (trackIndex -> eventId). Tracks without a hint get a fresh event.
    void setTargetHints(std::unordered_map<int, int64_t> hints);

    // Audio thread: queue a captured note.
    void captureNote(int64_t sample, uint8_t pitch, uint8_t velocity, bool noteOn);

    // Audio thread: queue a captured control message (CC / pitch bend /
    // pressure / program change). The raw status byte carries the channel.
    void captureControl(int64_t sample, uint8_t status, uint8_t data1, uint8_t data2);

    // GUI thread: while `recording` is true, apply captured notes to the armed
    // tracks' recording clips; when it turns false, finalize the recorded
    // event(s). Returns true if any clip changed (caller refreshes piano rolls).
    bool pump(Project& project, bool recording, int64_t playPosition,
              int64_t recordStartSample);

    bool isRecording() const { return m_begun; }

private:
    struct RecTarget {
        int trackIndex = -1;
        int64_t eventId = -1;
        bool created = false;
        std::unordered_map<uint8_t, int64_t> pendingNoteIds;
    };

    MidiEvent* resolveTarget(Project& project, RecTarget& target);
    RecTarget* findOrCreateTarget(Project& project, int trackIndex,
                                  int64_t recordStartSample, int64_t noteSample);
    void finish(Project& project, int64_t playPosition);
    int64_t noteStartTick(const Project& project, const RecTarget& target,
                          int64_t sample);
    // Add a captured CC / pitch-bend message to the clip at `tick`.
    bool recordControlMessage(MidiClip& clip, const MidiMessage& msg, int64_t tick);
    // Apply one captured message to a recording target's clip (notes pair up
    // via pendingNoteIds; control messages are appended).
    bool recordMessage(Project& project, RecTarget& target, const MidiMessage& msg,
                       int64_t tick);

    MidiRingBuffer<TimedMidiMessage> m_pending;
    std::unordered_map<int, int64_t> m_hints;
    std::vector<RecTarget> m_targets;
    int64_t m_recordStartSample = 0;
    bool m_begun = false;
};