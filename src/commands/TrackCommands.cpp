#include "TrackCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "plugin/PluginManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>

static QJsonObject trackToJson(const Track& track) {
    QJsonObject tObj;
    tObj["name"] = track.name();
    tObj["inputDeviceId"] = track.inputDeviceId();
    tObj["inputChannel"] = track.inputChannel();
    tObj["outputBusIndex"] = track.outputBusIndex();
    tObj["pan"] = track.pan();
    tObj["volume"] = track.volume();
    tObj["muted"] = track.isMuted();
    tObj["solo"] = track.isSolo();

    QJsonArray eventsArr;
    for (const auto& event : track.events()) {
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
        eventsArr.append(eObj);
    }
    tObj["events"] = eventsArr;
    if (track.pluginChain().count() > 0)
        tObj["plugins"] = track.pluginChain().toJson();
    return tObj;
}

static Track trackFromJson(const QJsonObject& tObj, PluginManager* manager = nullptr) {
    Track track(tObj["name"].toString());
    track.setInputDeviceId(tObj["inputDeviceId"].toInt(-1));
    track.setInputChannel(tObj["inputChannel"].toInt(0));
    track.setOutputBusIndex(tObj["outputBusIndex"].toInt(0));
    track.setPan(static_cast<float>(tObj["pan"].toDouble(0.0)));
    track.setVolume(static_cast<float>(tObj["volume"].toDouble(0.8)));
    track.setMuted(tObj["muted"].toBool(false));
    track.setSolo(tObj["solo"].toBool(false));

    const QJsonArray eventsArr = tObj["events"].toArray();
    for (const auto& eVal : eventsArr) {
        QJsonObject eObj = eVal.toObject();
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
        track.addEvent(event);
    }
    if (tObj.contains("plugins"))
        track.pluginChain().fromJson(tObj["plugins"].toObject(), manager);
    return track;
}

// --- AddTrackCommand ---

AddTrackCommand::AddTrackCommand(Project& project, int index)
    : m_project(project), m_index(index) {}

void AddTrackCommand::execute() {
    m_project.addTrack();
}

void AddTrackCommand::undo() {
    m_project.removeTrack(m_index);
}

// --- RemoveTrackCommand ---

RemoveTrackCommand::RemoveTrackCommand(Project& project, int index, PluginManager* manager)
    : m_project(project), m_index(index), m_manager(manager) {
    if (index >= 0 && index < static_cast<int>(m_project.tracks().size()))
        m_savedTrack = trackToJson(m_project.tracks()[index]);
}

void RemoveTrackCommand::execute() {
    m_project.removeTrack(m_index);
}

void RemoveTrackCommand::undo() {
    Track track = trackFromJson(m_savedTrack, m_manager);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.tracks().size())) {
        m_project.tracks().insert(m_project.tracks().begin() + m_index, std::move(track));
    } else {
        m_project.addTrack(track.name());
    }
}

// --- SetTrackVolumeCommand ---

SetTrackVolumeCommand::SetTrackVolumeCommand(Project& project, int trackIndex, float oldValue, float newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackVolumeCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setVolume(m_newValue);
}

void SetTrackVolumeCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setVolume(m_oldValue);
}

bool SetTrackVolumeCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetTrackVolumeCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetTrackPanCommand ---

SetTrackPanCommand::SetTrackPanCommand(Project& project, int trackIndex, float oldValue, float newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackPanCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setPan(m_newValue);
}

void SetTrackPanCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setPan(m_oldValue);
}

bool SetTrackPanCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetTrackPanCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetTrackMuteCommand ---

SetTrackMuteCommand::SetTrackMuteCommand(Project& project, int trackIndex, bool oldValue, bool newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackMuteCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setMuted(m_newValue);
}

void SetTrackMuteCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setMuted(m_oldValue);
}

// --- SetTrackSoloCommand ---

SetTrackSoloCommand::SetTrackSoloCommand(Project& project, int trackIndex, bool oldValue, bool newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackSoloCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setSolo(m_newValue);
}

void SetTrackSoloCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setSolo(m_oldValue);
}

// --- SetTrackOutputCommand ---

SetTrackOutputCommand::SetTrackOutputCommand(Project& project, int trackIndex, int oldValue, int newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackOutputCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setOutputBusIndex(m_newValue);
}

void SetTrackOutputCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setOutputBusIndex(m_oldValue);
}
