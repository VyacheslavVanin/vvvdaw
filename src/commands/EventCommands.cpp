#include "EventCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"

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
