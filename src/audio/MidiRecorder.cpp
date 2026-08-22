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
            int64_t endTick = MidiClip::kPPQ;
            for (const auto& note : clip->notes())
                endTick = std::max(endTick, note.endTick());
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
        uint8_t pitch = m.msg.data1;
        bool noteOn = m.msg.isNoteOn();

        for (int tIdx : armedTracks) {
            RecTarget* target = findOrCreateTarget(project, tIdx, m_recordStartSample, m.sample);
            if (!target)
                continue;
            MidiEvent* ev = resolveTarget(project, *target);
            if (!ev)
                continue;
            MidiClip* clip = ev->activeClip().get();
            if (!clip)
                continue;

            int64_t tick = noteStartTick(project, *target, m.sample);

            if (noteOn) {
                int64_t noteId = clip->addNote(pitch, m.msg.data2, tick, 1);
                target->pendingNoteIds[pitch] = noteId;
                clip->bumpRevision();
                changed = true;
            } else {
                auto it = target->pendingNoteIds.find(pitch);
                if (it != target->pendingNoteIds.end()) {
                    MidiNote* note = clip->findNote(it->second);
                    if (note) {
                        int64_t dur = std::max<int64_t>(1, tick - note->startTick);
                        note->durationTicks = dur;
                        clip->bumpRevision();
                        changed = true;
                    }
                    target->pendingNoteIds.erase(it);
                }
            }
        }
    }
    return changed;
}