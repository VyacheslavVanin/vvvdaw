#include "EventCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include <QJsonArray>
#include <QJsonObject>

static QJsonObject eventToJson(const AudioEvent& event) {
    QJsonObject eObj;
    if (event.clip())
        eObj["clipPath"] = event.clip()->filePath();
    eObj["startSample"] = static_cast<qint64>(event.startSample());
    eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
    eObj["durationSample"] = static_cast<qint64>(event.durationSample());
    if (!event.takes().empty()) {
        QJsonArray takesArr;
        for (const auto& take : event.takes())
            takesArr.append(take->filePath());
        eObj["takes"] = takesArr;
        eObj["activeTakeIndex"] = event.activeTakeIndex();
    }
    return eObj;
}

static AudioEvent eventFromJson(const QJsonObject& eObj) {
    AudioEvent event;
    QString clipPath = eObj["clipPath"].toString();
    if (!clipPath.isEmpty()) {
        auto clip = std::make_shared<AudioClip>(clipPath);
        if (clip->isValid())
            event.setClip(clip);
    }
    event.setStartSample(static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong()));
    event.setOffsetSample(static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong()));
    event.setDurationSample(static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong()));
    if (eObj.contains("takes")) {
        const QJsonArray takesArr = eObj["takes"].toArray();
        for (const auto& takeVal : takesArr) {
            QString takePath = takeVal.toString();
            if (!takePath.isEmpty()) {
                auto takeClip = std::make_shared<AudioClip>(takePath);
                if (takeClip->isValid())
                    event.takes().push_back(takeClip);
            }
        }
        event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
        if (event.activeTakeIndex() >= 0 && event.activeTakeIndex() < static_cast<int>(event.takes().size()))
            event.setClip(event.takes()[event.activeTakeIndex()]);
    }
    return event;
}

// --- AddEventCommand ---

AddEventCommand::AddEventCommand(Project& project, int trackIndex, QJsonObject eventJson)
    : m_project(project), m_trackIndex(trackIndex), m_eventJson(eventJson) {}

void AddEventCommand::execute() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    AudioEvent event = eventFromJson(m_eventJson);
    m_project.tracks()[m_trackIndex].addEvent(event);
}

void AddEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto& events = m_project.tracks()[m_trackIndex].events();
    if (!events.empty())
        m_project.tracks()[m_trackIndex].removeEvent(events.back().id());
}

// --- RemoveEventCommand ---

RemoveEventCommand::RemoveEventCommand(Project& project, int trackIndex, int64_t eventId)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_project.tracks().size())) {
        auto* ev = m_project.tracks()[trackIndex].findEvent(eventId);
        if (ev) m_savedEvent = eventToJson(*ev);
    }
}

void RemoveEventCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].removeEvent(m_eventId);
}

void RemoveEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    AudioEvent event = eventFromJson(m_savedEvent);
    m_project.tracks()[m_trackIndex].importEvent(event);
}

// --- MoveEventCommand ---

MoveEventCommand::MoveEventCommand(Project& project, int trackIndex, int64_t eventId,
                                   int64_t oldStart, int64_t newStart)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldStart(oldStart), m_newStart(newStart) {}

void MoveEventCommand::execute() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) ev->setStartSample(m_newStart);
}

void MoveEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) ev->setStartSample(m_oldStart);
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
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) {
        ev->setOffsetSample(m_newOffset);
        ev->setDurationSample(m_newDuration);
    }
}

void TrimEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) {
        ev->setOffsetSample(m_oldOffset);
        ev->setDurationSample(m_oldDuration);
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
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) ev->setActiveTakeIndex(m_newTake);
}

void SwitchTakeCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto* ev = m_project.tracks()[m_trackIndex].findEvent(m_eventId);
    if (ev) ev->setActiveTakeIndex(m_oldTake);
}
