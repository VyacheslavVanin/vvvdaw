#include "EventCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
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
                                   int64_t oldOffset, int64_t oldDuration,
                                   int64_t newOffset, int64_t newDuration)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldOffset(oldOffset), m_oldDuration(oldDuration),
      m_newOffset(newOffset), m_newDuration(newDuration) {}

void TrimEventCommand::execute() {
    if (Track* track = m_project.trackAt(m_trackIndex)) {
        if (AudioEvent* ev = track->findEvent(m_eventId)) {
            ev->setOffsetSample(m_newOffset);
            ev->setDurationSample(m_newDuration);
        }
    }
}

void TrimEventCommand::undo() {
    if (Track* track = m_project.trackAt(m_trackIndex)) {
        if (AudioEvent* ev = track->findEvent(m_eventId)) {
            ev->setOffsetSample(m_oldOffset);
            ev->setDurationSample(m_oldDuration);
        }
    }
}

bool TrimEventCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const TrimEventCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId) return false;
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
                                 int64_t cutSample)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_cutSample(cutSample) {
    if (Track* track = m_project.trackAt(trackIndex)) {
        if (AudioEvent* ev = track->findEvent(eventId)) {
            m_savedStart = ev->startSample();
            m_savedOffset = ev->offsetSample();
            m_savedDuration = ev->durationSample();
            m_savedSourceFrames = ev->sourceFrames();
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

    // Left part: the original event, truncated to the cut.
    AudioEvent right = *ev;
    ev->setDurationSample(cutRel);
    ev->setSourceFrames(cutSrc);

    // Right part: a copy of the original shifted to start at the cut.
    right.setStartSample(start + cutRel);
    right.setOffsetSample(ev->offsetSample() + cutSrc);
    right.setDurationSample(duration - cutRel);
    right.setSourceFrames(srcFrames - cutSrc);
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
    restored.setClip(m_savedClip);
    restored.takes() = m_savedTakes;
    restored.setActiveTakeIndex(m_savedActiveTakeIndex);
    track->importEvent(std::move(restored));
    m_didCut = false;
}
