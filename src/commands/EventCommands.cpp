#include "EventCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "core/TimeUtils.h"
#include <algorithm>
#include <cmath>

// --- AddEventCommand ---

AddEventCommand::AddEventCommand(Project& project, int trackIndex, QJsonObject eventJson)
    : m_project(project), m_trackIndex(trackIndex), m_eventJson(eventJson) {}

void AddEventCommand::execute() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track)
        return;
    track->addEvent(AudioEvent::fromJson(m_eventJson));
}

void AddEventCommand::undo() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track)
        return;
    if (!track->events().empty())
        track->removeEvent(track->events().back().id());
}

// --- RemoveEventCommand ---

RemoveEventCommand::RemoveEventCommand(Project& project, int trackIndex, int64_t eventId)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId) {
    if (Track* track = m_project.trackAt(trackIndex)) {
        if (AudioEvent* ev = track->findEvent(eventId))
            m_savedEvent = ev->toJson();
    }
}

void RemoveEventCommand::execute() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (track)
        track->removeEvent(m_eventId);
}

void RemoveEventCommand::undo() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track)
        return;
    track->importEvent(AudioEvent::fromJson(m_savedEvent));
}

// --- MoveEventCommand ---

MoveEventCommand::MoveEventCommand(Project& project, int trackIndex, int64_t eventId,
                                   int64_t oldStart, int64_t newStart)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldStart(oldStart), m_newStart(newStart) {}

void MoveEventCommand::execute() {
    if (Track* track = m_project.trackAt(m_trackIndex))
        if (AudioEvent* ev = track->findEvent(m_eventId))
            ev->setStartSample(m_newStart);
}

void MoveEventCommand::undo() {
    if (Track* track = m_project.trackAt(m_trackIndex))
        if (AudioEvent* ev = track->findEvent(m_eventId))
            ev->setStartSample(m_oldStart);
}

bool MoveEventCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const MoveEventCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId) return false;
    m_newStart = cmd->m_newStart;
    return true;
}

// --- TrimEventCommand ---

TrimEventCommand::TrimEventCommand(Project& project, int trackIndex, int64_t eventId,
                                   int64_t oldStart, int64_t newStart,
                                   int64_t oldOffset, int64_t oldDuration,
                                   int64_t newOffset, int64_t newDuration)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldStart(oldStart), m_newStart(newStart),
      m_oldOffset(oldOffset), m_oldDuration(oldDuration),
      m_newOffset(newOffset), m_newDuration(newDuration) {}

void TrimEventCommand::execute() {
    if (Track* track = m_project.trackAt(m_trackIndex)) {
        if (AudioEvent* ev = track->findEvent(m_eventId)) {
            ev->setStartSample(m_newStart);
            ev->setOffsetSample(m_newOffset);
            ev->setDurationSample(m_newDuration);
        }
    }
}

void TrimEventCommand::undo() {
    if (Track* track = m_project.trackAt(m_trackIndex)) {
        if (AudioEvent* ev = track->findEvent(m_eventId)) {
            ev->setStartSample(m_oldStart);
            ev->setOffsetSample(m_oldOffset);
            ev->setDurationSample(m_oldDuration);
        }
    }
}

bool TrimEventCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const TrimEventCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId) return false;
    m_newStart = cmd->m_newStart;
    m_newOffset = cmd->m_newOffset;
    m_newDuration = cmd->m_newDuration;
    return true;
}

// --- SwitchTakeCommand ---

SwitchTakeCommand::SwitchTakeCommand(Project& project, int trackIndex, int64_t eventId,
                                     int oldTake, int newTake)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldTake(oldTake), m_newTake(newTake) {}

void SwitchTakeCommand::execute() {
    if (Track* track = m_project.trackAt(m_trackIndex))
        if (AudioEvent* ev = track->findEvent(m_eventId))
            ev->setActiveTakeIndex(m_newTake);
}

void SwitchTakeCommand::undo() {
    if (Track* track = m_project.trackAt(m_trackIndex))
        if (AudioEvent* ev = track->findEvent(m_eventId))
            ev->setActiveTakeIndex(m_oldTake);
}

// --- CutEventCommand ---

CutEventCommand::CutEventCommand(Project& project, int trackIndex, int64_t eventId,
                                 int64_t cutSample, bool snapToGrid, double snapUnit)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_cutSample(cutSample), m_snapToGrid(snapToGrid), m_snapUnit(snapUnit) {
    if (Track* track = m_project.trackAt(trackIndex)) {
        if (AudioEvent* ev = track->findEvent(eventId)) {
            m_savedStart = ev->startSample();
            m_savedOffset = ev->offsetSample();
            m_savedDuration = ev->durationSample();
            m_savedSourceFrames = ev->sourceFrames();
            m_savedFadeIn = ev->fadeInSamples();
            m_savedFadeOut = ev->fadeOutSamples();
            m_savedClip = ev->clip();
            m_savedTakes = ev->takes();
            m_savedActiveTakeIndex = ev->activeTakeIndex();
        }
    }
}

void CutEventCommand::execute() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track) return;
    AudioEvent* ev = track->findEvent(m_eventId);
    if (!ev) return;

    const int64_t start = ev->startSample();
    const int64_t duration = ev->durationSample();
    if (m_cutSample <= start || m_cutSample >= start + duration || duration <= 1)
        return;

    // Number of source frames before the cut, preserving the source/timeline
    // ratio so the two parts sound exactly like the original when adjacent.
    const int64_t srcFrames = ev->sourceFrames() > 0 ? ev->sourceFrames() : duration;
    if (srcFrames <= 1)
        return;
    const int64_t cutRel = m_cutSample - start;
    int64_t cutSrc = static_cast<int64_t>(std::llround(
        static_cast<double>(cutRel) * static_cast<double>(srcFrames)
        / static_cast<double>(duration)));
    cutSrc = std::clamp<int64_t>(cutSrc, 1, srcFrames - 1);

    // Alignment of the split point to the nearest snap line. When the cut
    // lands after the line, the left piece is trimmed to it and the right
    // piece slides left to meet it; when it lands before the line, only the
    // right piece slides forward (leaving a gap). The right piece always keeps
    // its own source window (offset/sourceFrames) as in a plain cut.
    int64_t leftEndRel = cutRel;
    int64_t rightStart = m_cutSample;
    if (m_snapToGrid && m_snapUnit > 0.0) {
        const int64_t snapPos = TimeUtils::snapSample(m_cutSample, m_snapUnit);
        // Only snap when the line lies after the event start; otherwise the
        // left piece cannot be trimmed (or would collapse) and a plain cut is
        // the best approximation.
        if (snapPos > start) {
            if (snapPos <= m_cutSample && snapPos - start >= 1)
                leftEndRel = snapPos - start;
            rightStart = snapPos;
        }
    }
    if (leftEndRel < 1 || leftEndRel >= duration) {
        leftEndRel = cutRel;
        rightStart = m_cutSample;
    }
    int64_t leftSrcEnd = static_cast<int64_t>(std::llround(
        static_cast<double>(leftEndRel) * static_cast<double>(srcFrames)
        / static_cast<double>(duration)));
    leftSrcEnd = std::clamp<int64_t>(leftSrcEnd, 1, srcFrames - 1);

    // Left part: the original event, truncated to the (snapped) cut. The
    // original's outer fade-in is kept; the fade-out must not reach into the
    // new cut boundary (that junction is a fresh splice).
    AudioEvent right = *ev;
    ev->setDurationSample(leftEndRel);
    ev->setSourceFrames(leftSrcEnd);
    ev->setFadeOutSamples(0);

    // Right part: a copy of the original shifted to start at the (snapped) cut.
    // The original's outer fade-out is kept; the fade-in at the splice is
    // cleared so only the left piece's tail fades at the junction.
    right.setStartSample(rightStart);
    right.setOffsetSample(ev->offsetSample() + cutSrc);
    right.setDurationSample(duration - cutRel);
    right.setSourceFrames(srcFrames - cutSrc);
    right.setFadeInSamples(0);
    track->addEvent(std::move(right));
    m_rightEventId = track->events().back().id();
    m_didCut = true;
}

void CutEventCommand::undo() {
    if (!m_didCut) return;
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track) return;

    track->removeEvent(m_rightEventId);
    track->removeEvent(m_eventId);

    AudioEvent restored;
    restored.setId(m_eventId);
    restored.setStartSample(m_savedStart);
    restored.setOffsetSample(m_savedOffset);
    restored.setDurationSample(m_savedDuration);
    restored.setSourceFrames(m_savedSourceFrames);
    restored.setFadeInSamples(m_savedFadeIn);
    restored.setFadeOutSamples(m_savedFadeOut);
    restored.setClip(m_savedClip);
    restored.takes() = m_savedTakes;
    restored.setActiveTakeIndex(m_savedActiveTakeIndex);
    track->importEvent(std::move(restored));
    m_didCut = false;
}

// --- SetEventsFadeCommand ---

SetEventsFadeCommand::SetEventsFadeCommand(Project& project, int trackIndex,
                                           std::vector<int64_t> eventIds,
                                           int64_t fadeSamples)
    : m_project(project), m_trackIndex(trackIndex),
      m_eventIds(std::move(eventIds)), m_fadeSamples(fadeSamples) {
    Track* track = m_project.trackAt(trackIndex);
    if (!track)
        return;
    // Order by timeline position so consecutive ids are the adjacent events
    // whose shared boundaries get the fades.
    std::sort(m_eventIds.begin(), m_eventIds.end(),
              [track](int64_t a, int64_t b) {
                  const AudioEvent* ea = track->findEvent(a);
                  const AudioEvent* eb = track->findEvent(b);
                  if (!ea) return false;
                  if (!eb) return true;
                  return ea->startSample() < eb->startSample();
              });
    m_oldStates.reserve(m_eventIds.size());
    for (int64_t id : m_eventIds) {
        const AudioEvent* ev = track->findEvent(id);
        if (!ev) {
            m_oldStates.push_back({});
            continue;
        }
        m_oldStates.push_back({ev->fadeInSamples(), ev->fadeOutSamples()});
    }
}

void SetEventsFadeCommand::execute() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track || m_eventIds.size() < 2)
        return;
    for (size_t i = 0; i + 1 < m_eventIds.size(); ++i) {
        AudioEvent* left = track->findEvent(m_eventIds[i]);
        AudioEvent* right = track->findEvent(m_eventIds[i + 1]);
        if (!left || !right)
            continue;
        const int64_t leftLen = left->durationSample() > 1
            ? left->durationSample() - 1 : 0;
        const int64_t rightLen = right->durationSample() > 1
            ? right->durationSample() - 1 : 0;
        left->setFadeOutSamples(std::min<int64_t>(m_fadeSamples, leftLen));
        right->setFadeInSamples(std::min<int64_t>(m_fadeSamples, rightLen));
    }
}

void SetEventsFadeCommand::undo() {
    Track* track = m_project.trackAt(m_trackIndex);
    if (!track)
        return;
    for (size_t i = 0; i < m_eventIds.size() && i < m_oldStates.size(); ++i) {
        if (AudioEvent* ev = track->findEvent(m_eventIds[i])) {
            ev->setFadeInSamples(m_oldStates[i].oldFadeIn);
            ev->setFadeOutSamples(m_oldStates[i].oldFadeOut);
        }
    }
}

// --- MoveEventToTrackCommand ---

MoveEventToTrackCommand::MoveEventToTrackCommand(Project& project, int srcTrackIndex,
                                                 int dstTrackIndex, int64_t eventId,
                                                 int64_t oldStart, int64_t newStart)
    : m_project(project), m_srcTrackIndex(srcTrackIndex), m_dstTrackIndex(dstTrackIndex),
      m_eventId(eventId), m_oldStart(oldStart), m_newStart(newStart) {}

void MoveEventToTrackCommand::execute() {
    Track* src = m_project.trackAt(m_srcTrackIndex);
    Track* dst = m_project.trackAt(m_dstTrackIndex);
    if (!src || !dst) return;

    if (src->type() == Track::Type::Midi) {
        MidiEvent* ev = src->findMidiEvent(m_eventId);
        if (!ev) return;
        ev->setStartSample(m_newStart);
        dst->importMidiEvent(*ev);
        src->removeMidiEvent(m_eventId);
    } else {
        AudioEvent* ev = src->findEvent(m_eventId);
        if (!ev) return;
        ev->setStartSample(m_newStart);
        dst->importEvent(*ev);
        src->removeEvent(m_eventId);
    }
}

void MoveEventToTrackCommand::undo() {
    Track* src = m_project.trackAt(m_srcTrackIndex);
    Track* dst = m_project.trackAt(m_dstTrackIndex);
    if (!src || !dst) return;

    if (dst->type() == Track::Type::Midi) {
        MidiEvent* ev = dst->findMidiEvent(m_eventId);
        if (!ev) return;
        ev->setStartSample(m_oldStart);
        src->importMidiEvent(*ev);
        dst->removeMidiEvent(m_eventId);
    } else {
        AudioEvent* ev = dst->findEvent(m_eventId);
        if (!ev) return;
        ev->setStartSample(m_oldStart);
        src->importEvent(*ev);
        dst->removeEvent(m_eventId);
    }
}
