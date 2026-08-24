#include "AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioClip.h"
#include "model/AudioEvent.h"
#include "model/Instrument.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "AudioUtils.h"
#include "AudioEngineInternals.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <QDebug>

using vvvdaw::TransportState;
using namespace vvvdaw::audioengine;

namespace {

// Encode a stored control event into raw MIDI bytes for the given channel.
struct EncodedControl {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

EncodedControl encodeControlEvent(const MidiControlEvent& ce, uint8_t channel) {
    EncodedControl out;
    if (ce.kind == MidiControlEvent::Kind::PitchBend) {
        out.status = static_cast<uint8_t>(0xE0 | channel);
        out.data1 = static_cast<uint8_t>(ce.value & 0x7F);
        out.data2 = static_cast<uint8_t>((ce.value >> 7) & 0x7F);
    } else {
        out.status = static_cast<uint8_t>(0xB0 | channel);
        out.data1 = ce.number;
        out.data2 = static_cast<uint8_t>(ce.value & 0x7F);
    }
    return out;
}

// Default value when a clip has automation of the given kind but no event at
// or before the playhead: CCs reset to 0, pitch bend to center (8192).
int controlDefaultValue(MidiControlEvent::Kind kind) {
    return kind == MidiControlEvent::Kind::PitchBend ? 8192 : 0;
}

// Output destination of an audible MIDI track. `valid` is false when the track
// is silent (muted / solo-skipped / no target) and nothing should be sent.
struct MidiTarget {
    int destIndex = -1;
    bool toInstrument = false;
    uint8_t channel = 0;
    bool valid = false;
};

MidiTarget resolveMidiTarget(const Project* proj, const Track& track, bool anySolo) {
    MidiTarget t;
    int instIdx = track.instrumentIndex();
    t.toInstrument = instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size());
    bool targetMuted = t.toInstrument && proj->instruments()[instIdx].isMuted();
    if (track.isMuted() || targetMuted || (anySolo && !track.isSolo()))
        return t;
    t.destIndex = t.toInstrument ? instIdx : track.midiOutputDeviceId();
    t.channel = static_cast<uint8_t>(track.midiChannel());
    t.valid = true;
    return t;
}

} // namespace

void AudioEngine::pollMidiInput(Project* proj, int64_t pos, unsigned long frameCount,
                                vvvdaw::TransportState state) {
    if (!m_midiInput.hasPendingNotes())
        return;

    MidiMessage msgs[64];
    int n = m_midiInput.pollNotes(msgs, 64);
    if (n == 0)
        return;

    bool recording = (state == TransportState::Recording);
    int previewTrack = m_midiPreviewTrack.load(std::memory_order_acquire);
    bool canPreview = previewTrack >= 0 && previewTrack < static_cast<int>(proj->tracks().size())
        && proj->tracks()[static_cast<size_t>(previewTrack)].type() == Track::Type::Midi;

    for (int i = 0; i < n; ++i)
        handleMidiInputMessage(msgs[i], pos, recording, canPreview, previewTrack);
}

void AudioEngine::handleMidiInputMessage(const MidiMessage& m, int64_t pos,
                                         bool recording, bool canPreview,
                                         int previewTrack) {
    recordMidiMessage(m, pos, recording);
    previewMidiMessage(m, previewTrack, canPreview);
}

void AudioEngine::recordMidiMessage(const MidiMessage& m, int64_t pos, bool recording) {
    if (!recording)
        return;
    if (m.isNoteOn()) {
        m_midiRecorder.captureNote(pos, m.data1, m.data2, true);
        return;
    }
    if (m.isNoteOff()) {
        m_midiRecorder.captureNote(pos, m.data1, 0, false);
        return;
    }
    if (m.isCc() || m.isPitchBend())
        m_midiRecorder.captureControl(pos, m.status, m.data1, m.data2);
}

void AudioEngine::previewMidiMessage(const MidiMessage& m, int previewTrack, bool canPreview) {
    if (!canPreview)
        return;
    if (m.isNoteOn()) {
        previewNoteOn(previewTrack, m.data1, m.data2);
        return;
    }
    if (m.isNoteOff()) {
        previewNoteOff(previewTrack, m.data1);
        return;
    }
    // Continuous controllers: preview them live so the instrument responds
    // while playing the keyboard.
    if (m.isCc() || m.isPitchBend())
        previewControl(previewTrack, m.status, m.data1, m.data2);
}


void AudioEngine::queueMidiEvent(int destIndex, bool toInstrument, uint8_t status,
                                 uint8_t pitch, uint8_t velocity, int sampleOffset) {
    if (toInstrument) {
        if (destIndex >= 0 && destIndex < static_cast<int>(m_instrumentMidi.size())) {
            MidiMessage m;
            m.sampleOffset = sampleOffset;
            m.status = status;
            m.data1 = pitch;
            m.data2 = velocity;
            m_instrumentMidi[destIndex].push_back(m);
        }
    } else if (destIndex >= 0) {
        m_midiOutput.send(destIndex, status, pitch, velocity);
    }
}

void AudioEngine::sendNoteOn(int destIndex, bool toInstrument, uint8_t channel,
                             uint8_t pitch, uint8_t velocity, int sampleOffset) {
    queueMidiEvent(destIndex, toInstrument, static_cast<uint8_t>(0x90 | channel),
                   pitch, velocity, sampleOffset);
}


void AudioEngine::sendNoteOff(int destIndex, bool toInstrument, uint8_t channel,
                              uint8_t pitch, int sampleOffset) {
    queueMidiEvent(destIndex, toInstrument, static_cast<uint8_t>(0x80 | channel),
                   pitch, 0, sampleOffset);
}


void AudioEngine::sendActiveNoteOff(const ActiveMidiNote& note, int sampleOffset) {
    sendNoteOff(note.destIndex, note.toInstrument, note.channel, note.pitch, sampleOffset);
}


void AudioEngine::flushActiveMidiNotes() {
    for (const auto& an : m_activeMidiNotes)
        sendActiveNoteOff(an, 0);
    m_activeMidiNotes.clear();
}


void AudioEngine::scheduleMidiTracks(Project* proj, unsigned long frameCount, int64_t pos) {
    bool anySolo = anyTrackSolo(proj);

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.type() != Track::Type::Midi) { ++trackIndex; continue; }

        int instIdx = track.instrumentIndex();
        bool toInstrument = (instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size()));
        bool targetMuted = toInstrument && proj->instruments()[instIdx].isMuted();

        auto flushTrackNotes = [&](int tIndex) {
            for (auto it = m_activeMidiNotes.begin(); it != m_activeMidiNotes.end();) {
                if (it->trackIndex == tIndex) {
                    sendActiveNoteOff(*it, 0);
                    it = m_activeMidiNotes.erase(it);
                } else {
                    ++it;
                }
            }
        };

        // Muted track, muted target instrument, or track skipped by solo: release
        // notes still held by the destination so they do not keep ringing (and do
        // not resume sounding after unmute).
        if (track.isMuted() || targetMuted || (anySolo && !track.isSolo())) {
            flushTrackNotes(trackIndex);
            ++trackIndex;
            continue;
        }

        int deviceId = track.midiOutputDeviceId();
        int destIdx = toInstrument ? instIdx : deviceId;
        uint8_t channel = static_cast<uint8_t>(track.midiChannel());

        for (const auto& event : track.midiEvents()) {
            auto clip = event.activeClip();
            if (!clip) continue;

            int64_t eventEnd = event.startSample() + event.durationSample();

            // The event has ended: cut off every note from it that is still
            // sounding. Notes drawn past the clip boundary have their own
            // note-off beyond the event end, so without this they would ring
            // forever once the event stops being processed.
            if (pos >= eventEnd) {
                for (auto it = m_activeMidiNotes.begin(); it != m_activeMidiNotes.end();) {
                    if (it->trackIndex == trackIndex && it->eventId == event.id()) {
                        sendActiveNoteOff(*it, 0);
                        it = m_activeMidiNotes.erase(it);
                    } else {
                        ++it;
                    }
                }
                continue;
            }
            if (pos + static_cast<int64_t>(frameCount) <= event.startSample())
                continue;

            int64_t offsetTicks = proj->samplesToTicks(event.offsetSample());
            scheduleControlEvents(proj, trackIndex, event, offsetTicks, destIdx,
                                  toInstrument, channel, pos, frameCount);
            for (const auto& note : clip->notes()) {
                int64_t noteStart = event.startSample()
                    + proj->ticksToSamples(note.startTick - offsetTicks);
                int64_t noteEnd = event.startSample()
                    + proj->ticksToSamples(note.endTick() - offsetTicks);

                // Note onset at/after the event's end: never sound it (its
                // onset would coincide with the cut point and it could never
                // receive a note-off).
                if (noteStart >= eventEnd)
                    continue;
                // Clamp the note to the event boundary so the note-off is
                // scheduled while the event is still processed.
                if (noteEnd > eventEnd)
                    noteEnd = eventEnd;

                // Allow noteEnd == pos: a note ending exactly on a block
                // boundary must still receive its note-off (at offset 0 of
                // this block), otherwise it would ring forever.
                if (noteEnd < pos || noteStart >= pos + static_cast<int64_t>(frameCount))
                    continue;

                bool alreadyActive = false;
                for (auto& an : m_activeMidiNotes) {
                    if (an.trackIndex == trackIndex && an.eventId == event.id()
                        && an.noteId == note.id) {
                        alreadyActive = true;
                        break;
                    }
                }

                if (!alreadyActive && noteStart >= pos) {
                    // A note whose onset lies before the current playback
                    // position and which is not already sounding was missed
                    // (seek / stop / loop wrap). Do not catch it up: it should
                    // not play, and no note-off is needed since the synth never
                    // received its note-on. The guard above skips exactly that
                    // case, so the onset always lands in this block.
                    int off = static_cast<int>(noteStart - pos);
                    if (off < static_cast<int>(frameCount)) {
                        sendNoteOn(toInstrument ? instIdx : deviceId, toInstrument,
                                   channel, static_cast<uint8_t>(note.pitch),
                                   static_cast<uint8_t>(note.velocity), off);
                        ActiveMidiNote an;
                        an.trackIndex = trackIndex;
                        an.eventId = event.id();
                        an.noteId = note.id;
                        an.destIndex = destIdx;
                        an.toInstrument = toInstrument;
                        an.channel = channel;
                        an.pitch = static_cast<uint8_t>(note.pitch);
                        m_activeMidiNotes.push_back(an);
                    }
                }

                if (noteEnd >= pos && noteEnd < pos + static_cast<int64_t>(frameCount)) {
                    int off = static_cast<int>(noteEnd - pos);
                    ActiveMidiNote an;
                    an.trackIndex = trackIndex;
                    an.eventId = event.id();
                    an.noteId = note.id;
                    an.destIndex = destIdx;
                    an.toInstrument = toInstrument;
                    an.channel = channel;
                    an.pitch = static_cast<uint8_t>(note.pitch);
                    sendActiveNoteOff(an, off);
                    auto it = std::remove_if(m_activeMidiNotes.begin(), m_activeMidiNotes.end(),
                        [&](const ActiveMidiNote& a) {
                            return a.trackIndex == trackIndex && a.eventId == event.id()
                                && a.noteId == note.id;
                        });
                    m_activeMidiNotes.erase(it, m_activeMidiNotes.end());
                }
            }
        }
        ++trackIndex;
    }
}

void AudioEngine::scheduleControlEvents(const Project* proj, int trackIndex,
                                        const MidiEvent& event, int64_t offsetTicks,
                                        int destIdx, bool toInstrument, uint8_t channel,
                                        int64_t pos, unsigned long frameCount) {
    auto clip = event.activeClip();
    if (!clip)
        return;

    int64_t eventEnd = event.startSample() + event.durationSample();
    int64_t blockEnd = pos + static_cast<int64_t>(frameCount);

    for (const auto& ce : clip->controlEvents()) {
        int64_t sample = event.startSample()
            + proj->ticksToSamples(ce.startTick - offsetTicks);
        // Onset at/after the event's end never sounds.
        if (sample >= eventEnd)
            continue;
        if (sample < pos || sample >= blockEnd)
            continue;
        if (controlEventSent(trackIndex, event.id(), ce.id))
            continue;

        EncodedControl enc = encodeControlEvent(ce, channel);
        int off = static_cast<int>(sample - pos);
        queueMidiEvent(destIdx, toInstrument, enc.status, enc.data1, enc.data2, off);
        SentControlEvent sc;
        sc.trackIndex = trackIndex;
        sc.eventId = event.id();
        sc.controlId = ce.id;
        m_sentControlEvents.push_back(sc);
    }
}

bool AudioEngine::controlEventSent(int trackIndex, int64_t eventId, int64_t controlId) const {
    for (const auto& sc : m_sentControlEvents) {
        if (sc.trackIndex == trackIndex && sc.eventId == eventId
            && sc.controlId == controlId)
            return true;
    }
    return false;
}

void AudioEngine::applyEventControlState(const Project* proj, const MidiEvent& event,
                                         int trackIndex, int destIdx, bool toInstrument,
                                         uint8_t channel, int64_t pos) {
    auto clip = event.activeClip();
    if (!clip)
        return;
    int64_t offsetTicks = proj->samplesToTicks(event.offsetSample());

    using Key = std::pair<uint8_t, uint8_t>; // (kind, number)
    std::map<Key, const MidiControlEvent*> hasKey;
    std::map<Key, const MidiControlEvent*> latest;
    for (const auto& ce : clip->controlEvents()) {
        Key key(static_cast<uint8_t>(ce.kind), ce.number);
        hasKey[key] = &ce;
        int64_t sample = event.startSample()
            + proj->ticksToSamples(ce.startTick - offsetTicks);
        if (sample > pos)
            continue;
        auto it = latest.find(key);
        if (it == latest.end() || it->second->startTick < ce.startTick)
            latest[key] = &ce;
    }

    for (const auto& [key, ce] : latest) {
        EncodedControl enc = encodeControlEvent(*ce, channel);
        queueMidiEvent(destIdx, toInstrument, enc.status, enc.data1, enc.data2, 0);
        SentControlEvent sc;
        sc.trackIndex = trackIndex;
        sc.eventId = event.id();
        sc.controlId = ce->id;
        m_sentControlEvents.push_back(sc);
    }
    // Keys automated in the clip but without an event at/before pos: reset to
    // the default so no stale value from an earlier position (or another clip)
    // leaks through.
    for (const auto& [key, ce] : hasKey) {
        if (latest.count(key))
            continue;
        MidiControlEvent def;
        def.kind = ce->kind;
        def.number = ce->number;
        def.value = controlDefaultValue(ce->kind);
        def.startTick = -1;
        def.id = -1;
        EncodedControl enc = encodeControlEvent(def, channel);
        queueMidiEvent(destIdx, toInstrument, enc.status, enc.data1, enc.data2, 0);
    }
}

void AudioEngine::reapplyControlState(Project* proj, int64_t pos) {
    m_sentControlEvents.clear();
    bool anySolo = anyTrackSolo(proj);

    int trackIndex = 0;
    for (const auto& track : proj->tracks()) {
        if (track.type() != Track::Type::Midi) { ++trackIndex; continue; }

        MidiTarget target = resolveMidiTarget(proj, track, anySolo);
        if (!target.valid) {
            ++trackIndex;
            continue;
        }

        for (const auto& event : track.midiEvents()) {
            auto clip = event.activeClip();
            if (!clip)
                continue;
            int64_t eventEnd = event.startSample() + event.durationSample();
            if (pos >= eventEnd || pos < event.startSample())
                continue;
            applyEventControlState(proj, event, trackIndex, target.destIndex,
                                   target.toInstrument, target.channel, pos);
        }
        ++trackIndex;
    }
}


void AudioEngine::refreshMidiOutputs() {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj) return;

    std::set<int> needed;
    for (const auto& track : proj->tracks()) {
        if (track.type() == Track::Type::Midi && track.midiOutputDeviceId() >= 0)
            needed.insert(track.midiOutputDeviceId());
    }

    for (int id : needed) {
        if (!m_openMidiDevices.count(id))
            m_midiOutput.open(id);
    }
    for (int id : m_openMidiDevices) {
        if (!needed.count(id))
            m_midiOutput.close(id);
    }
    m_openMidiDevices = std::move(needed);
}


void AudioEngine::panicMidi() {
    for (int id : m_openMidiDevices)
        m_midiOutput.sendAllNotesOff(id);
}


void AudioEngine::setMidiPreviewTrack(int trackIndex) {
    m_midiPreviewTrack.store(trackIndex, std::memory_order_release);
}


void AudioEngine::setMidiTransportControls(const MidiTransportControls& controls) {
    m_midiInput.setTransportControls(controls);
}


void AudioEngine::setMidiInputDevice(int deviceId) {
    if (deviceId == m_midiInputDeviceId)
        return;
    if (deviceId >= 0)
        m_midiInput.open(deviceId);
    else
        m_midiInput.closeAll();
    m_midiInputDeviceId = deviceId;
}


void AudioEngine::setMidiLearnTarget(MidiLearnTarget target) {
    m_midiInput.setLearnTarget(target);
}


MidiLearnTarget AudioEngine::midiLearnTarget() const {
    return m_midiInput.learnTarget();
}


bool AudioEngine::popLearnedMidiControl(MidiTransportControls& out) {
    return m_midiInput.popLearned(out);
}


std::vector<MidiTransportCommand> AudioEngine::takeMidiTransportCommands() {
    MidiTransportCommand cmds[16];
    int n = m_midiInput.pollTransport(cmds, 16);
    return std::vector<MidiTransportCommand>(cmds, cmds + n);
}


void AudioEngine::previewNoteOn(int trackIndex, int pitch, int velocity) {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj || trackIndex < 0 || trackIndex >= static_cast<int>(proj->tracks().size()))
        return;
    const auto& track = proj->tracks()[trackIndex];
    if (track.type() != Track::Type::Midi) return;

    int instIdx = track.instrumentIndex();
    bool toInstrument = instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size());
    int target = toInstrument ? instIdx : track.midiOutputDeviceId();
    if (!toInstrument && target < 0) return;

    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (n.trackIndex == trackIndex && n.pitch == static_cast<uint8_t>(pitch)) {
            n.velocity = static_cast<uint8_t>(velocity);
            return;
        }
    }
    PreviewHeldNote n;
    n.trackIndex = trackIndex;
    n.channel = static_cast<uint8_t>(track.midiChannel());
    n.pitch = static_cast<uint8_t>(pitch);
    n.velocity = static_cast<uint8_t>(velocity);
    n.target = target;
    n.toInstrument = toInstrument;
    m_previewHeld.push_back(n);
    ++m_previewCount;
}


void AudioEngine::previewNoteOff(int trackIndex, int pitch) {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (n.trackIndex == trackIndex && n.pitch == static_cast<uint8_t>(pitch)
            && !n.offPending) {
            n.offPending = true;
            return;
        }
    }
}

void AudioEngine::previewControl(int trackIndex, uint8_t status, uint8_t data1,
                                 uint8_t data2) {
    auto* proj = m_project.load(std::memory_order_acquire);
    if (!proj || trackIndex < 0 || trackIndex >= static_cast<int>(proj->tracks().size()))
        return;
    const auto& track = proj->tracks()[trackIndex];
    if (track.type() != Track::Type::Midi) return;

    int instIdx = track.instrumentIndex();
    bool toInstrument = instIdx >= 0 && instIdx < static_cast<int>(proj->instruments().size());
    int target = toInstrument ? instIdx : track.midiOutputDeviceId();
    if (!toInstrument && target < 0) return;

    std::lock_guard<std::mutex> lock(m_previewMutex);
    PreviewControl c;
    c.target = target;
    c.toInstrument = toInstrument;
    c.channel = static_cast<uint8_t>(track.midiChannel());
    c.status = static_cast<uint8_t>((status & 0xF0) | c.channel);
    c.data1 = data1;
    c.data2 = data2;
    m_previewControls.push_back(c);
    ++m_previewControlCount;
}


void AudioEngine::cancelPreviewNotes(int trackIndex) {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    for (auto& n : m_previewHeld) {
        if (trackIndex < 0 || n.trackIndex == trackIndex)
            n.offPending = true;
    }
}


void AudioEngine::injectPreviewMidi() {
    std::lock_guard<std::mutex> lock(m_previewMutex);

    // Flush latched CC / pitch-bend preview messages into the instrument
    // buffer (or external device) so they survive the per-block buffer clear.
    for (const auto& c : m_previewControls) {
        if (c.target >= 0)
            queueMidiEvent(c.target, c.toInstrument, c.status, c.data1, c.data2, 0);
    }
    m_previewControls.clear();
    m_previewControlCount.store(0);

    if (m_previewHeld.empty())
        return;

    for (auto& n : m_previewHeld) {
        if (n.offPending) {
            sendNoteOff(n.target, n.toInstrument, n.channel, n.pitch);
            continue;
        }
        if (n.noteOnSent) continue;

        if (n.target >= 0) {
            sendNoteOn(n.target, n.toInstrument, n.channel, n.pitch, n.velocity);
            n.noteOnSent = true;
        } else {
            n.offPending = true; // nowhere to play it: drop
        }
    }

    m_previewHeld.erase(
        std::remove_if(m_previewHeld.begin(), m_previewHeld.end(),
                       [](const PreviewHeldNote& n) { return n.offPending; }),
        m_previewHeld.end());
    if (m_previewHeld.empty())
        m_previewCount.store(0);
}
