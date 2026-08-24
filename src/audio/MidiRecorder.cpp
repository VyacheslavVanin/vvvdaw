#include "MidiRecorder.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include "model/MidiClip.h"
#include <algorithm>
#include <utility>

MidiRecorder::MidiRecorder() = default;

void MidiRecorder::setTargetHints(std::unordered_map<int, int64_t> hints) {
    m_hints = std::move(hints);
}

void MidiRecorder::captureNote(int64_t sample, uint8_t pitch, uint8_t velocity,
                               bool noteOn) {
    TimedMidiMessage m;
    m.sample = sample;
    m.msg.status = noteOn ? static_cast<uint8_t>(0x90) : static_cast<uint8_t>(0x80);
    m.msg.data1 = pitch;
    m.msg.data2 = noteOn ? velocity : 0;
    m_pending.push(m);
}

void MidiRecorder::captureControl(int64_t sample, uint8_t status, uint8_t data1,
                                  uint8_t data2) {
    TimedMidiMessage m;
    m.sample = sample;
    m.msg.status = status;
    m.msg.data1 = data1;
    m.msg.data2 = data2;
    m_pending.push(m);
}

MidiEvent* MidiRecorder::resolveTarget(Project& project, RecTarget& target) {
    Track* track = project.trackAt(target.trackIndex);
    if (!track)
        return nullptr;
    return track->findMidiEvent(target.eventId);
}

int64_t MidiRecorder::noteStartTick(const Project& project, const RecTarget& target,
                                    int64_t sample) {
    MidiEvent* ev = resolveTarget(const_cast<Project&>(project), const_cast<RecTarget&>(target));
    if (!ev)
        return 0;
    int64_t offsetTicks = project.samplesToTicks(ev->offsetSample());
    int64_t tick = project.samplesToTicks(sample - ev->startSample()) + offsetTicks;
    return std::max<int64_t>(0, tick);
}

bool MidiRecorder::recordMessage(Project& project, RecTarget& target,
                                 const MidiMessage& msg, int64_t tick) {
    MidiEvent* ev = resolveTarget(project, target);
    if (!ev)
        return false;
    MidiClip* clip = ev->activeClip().get();
    if (!clip)
        return false;

    if (msg.isNoteOn()) {
        int64_t noteId = clip->addNote(msg.data1, msg.data2, tick, 1);
        target.pendingNoteIds[msg.data1] = noteId;
        clip->bumpRevision();
        return true;
    }
    if (msg.isNoteOff()) {
        auto it = target.pendingNoteIds.find(msg.data1);
        if (it == target.pendingNoteIds.end())
            return false;
        MidiNote* note = clip->findNote(it->second);
        if (note) {
            note->durationTicks = std::max<int64_t>(1, tick - note->startTick);
            clip->bumpRevision();
        }
        target.pendingNoteIds.erase(it);
        return note != nullptr;
    }
    if (msg.isCc() || msg.isPitchBend())
        return recordControlMessage(*clip, msg, tick);
    return false;
}

bool MidiRecorder::recordControlMessage(MidiClip& clip, const MidiMessage& msg, int64_t tick) {
    MidiControlEvent::Kind kind;
    uint8_t number;
    int value;
    if (msg.isPitchBend()) {
        kind = MidiControlEvent::Kind::PitchBend;
        number = 0;
        value = static_cast<int>(msg.data1) | (static_cast<int>(msg.data2) << 7);
    } else {
        kind = MidiControlEvent::Kind::ControlChange;
        number = msg.data1;
        value = msg.data2;
    }
    clip.addControlEvent(kind, number, value, tick);
    clip.bumpRevision();
    return true;
}

MidiRecorder::RecTarget* MidiRecorder::findOrCreateTarget(Project& project, int trackIndex,
                                                          int64_t recordStartSample,
                                                          int64_t noteSample) {
    for (auto& t : m_targets) {
        if (t.trackIndex == trackIndex)
            return &t;
    }

    RecTarget target;
    target.trackIndex = trackIndex;

    // Prefer the open piano-roll event on this track while the note lands
    // inside it, so recording shows up live in the piano roll.
    auto hintIt = m_hints.find(trackIndex);
    if (hintIt != m_hints.end()) {
        Track* track = project.trackAt(trackIndex);
        if (track) {
            MidiEvent* ev = track->findMidiEvent(hintIt->second);
            if (ev && ev->activeClip() && noteSample >= ev->startSample()
                && noteSample < ev->endSample())
                target.eventId = ev->id();
        }
    }

    if (target.eventId < 0) {
        Track* track = project.trackAt(trackIndex);
        if (!track)
            return nullptr;

        MidiEvent ev;
        ev.setStartSample(recordStartSample);
        ev.setOffsetSample(0);
        auto clip = std::make_shared<MidiClip>();
        clip->setLengthTicks(MidiClip::kPPQ);
        ev.setClip(clip);
        ev.setDurationSample(project.ticksToSamples(MidiClip::kPPQ)); // refined on finish
        track->addMidiEvent(ev);
        target.eventId = track->midiEvents().back().id();
        target.created = true;
    }

    m_targets.push_back(target);
    return &m_targets.back();
}

void MidiRecorder::finish(Project& project, int64_t playPosition) {
    for (auto& target : m_targets) {
        MidiEvent* ev = resolveTarget(project, target);
        if (!ev)
            continue;
        MidiClip* clip = ev->activeClip().get();
        if (!clip)
            continue;

        // Release notes whose note-off never arrived with the current duration.
        int64_t releaseTick = noteStartTick(project, target, playPosition);
        for (auto& [pitch, noteId] : target.pendingNoteIds) {
            MidiNote* note = clip->findNote(noteId);
            if (note) {
                int64_t dur = std::max<int64_t>(1, releaseTick - note->startTick);
                note->durationTicks = dur;
            }
        }
        target.pendingNoteIds.clear();

        if (target.created) {
            // Preserve the recorded extent so a session without notes does not
            // snap the event back to a single PPQ on stop. lengthTicks() also
            // covers control events (CC / pitch bend) recorded during the take.
            int64_t endTick = std::max<int64_t>(clip->lengthTicks(), MidiClip::kPPQ);
            int64_t recordedTicks = project.samplesToTicks(playPosition - m_recordStartSample);
            endTick = std::max(endTick, recordedTicks);
            clip->setLengthTicks(endTick);
            ev->setDurationSample(project.ticksToSamples(endTick));
            ev->setStartSample(m_recordStartSample);
        }
    }
}

bool MidiRecorder::pump(Project& project, bool recording, int64_t playPosition,
                        int64_t recordStartSample) {
    // Fast path: nothing to do (idle, not recording) — avoid taking the project
    // write lock every GUI tick so it cannot contend with the audio thread.
    if (!recording && !m_begun && m_pending.empty())
        return false;

    // All model access runs under the project write lock: the audio thread may
    // be reading tracks under a shared lock while notes are being recorded.
    auto writeLock = project.writeLock();

    if (!recording) {
        if (m_begun) {
            finish(project, playPosition);
            m_begun = false;
            m_targets.clear();
        }
        // Drop any notes captured after the last pump so they cannot leak into
        // the next recording session.
        TimedMidiMessage stale;
        while (m_pending.pop(stale)) {}
        return false;
    }

    if (!m_begun) {
        m_begun = true;
        m_recordStartSample = recordStartSample;
        m_targets.clear();
        // Show the recording event immediately on the armed MIDI tracks, even
        // before the first note arrives. Tracks with an open piano-roll hint
        // are left to findOrCreateTarget() so notes keep landing in that event.
        for (size_t i = 0; i < project.tracks().size(); ++i) {
            const auto& track = project.tracks()[i];
            if (track.type() != Track::Type::Midi || !track.isRecordArmed())
                continue;
            if (m_hints.find(static_cast<int>(i)) != m_hints.end())
                continue;
            findOrCreateTarget(project, static_cast<int>(i), recordStartSample,
                               recordStartSample);
        }
    }

    std::vector<int> armedTracks;
    for (size_t i = 0; i < project.tracks().size(); ++i) {
        const auto& track = project.tracks()[i];
        if (track.type() == Track::Type::Midi && track.isRecordArmed())
            armedTracks.push_back(static_cast<int>(i));
    }
    if (armedTracks.empty())
        return false;

    bool changed = false;
    TimedMidiMessage m;
    while (m_pending.pop(m)) {
        for (int tIdx : armedTracks) {
            RecTarget* target = findOrCreateTarget(project, tIdx, m_recordStartSample, m.sample);
            if (!target)
                continue;
            int64_t tick = noteStartTick(project, *target, m.sample);
            if (recordMessage(project, *target, m.msg, tick))
                changed = true;
        }
    }

    // Grow the recorded events to keep up with the playhead so the clip is
    // visibly recording as it happens.
    for (auto& target : m_targets) {
        if (!target.created)
            continue;
        MidiEvent* ev = resolveTarget(project, target);
        if (!ev)
            continue;
        int64_t grown = std::max<int64_t>(ev->durationSample(),
                                          playPosition - ev->startSample());
        ev->setDurationSample(grown);
    }
    return changed;
}