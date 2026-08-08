#include "TrackCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/MidiEvent.h"
#include "plugin/PluginManager.h"
#include "core/Constants.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>

static QJsonObject trackToJson(const Track& track) {
    QJsonObject tObj;
    tObj["name"] = track.name();
    tObj["type"] = track.type() == Track::Type::Midi ? "midi" : "audio";
    tObj["channels"] = track.channels();
    tObj["inputDeviceId"] = track.inputDeviceId();
    tObj["inputChannel"] = track.inputChannel();
    tObj["outputBusIndex"] = track.outputBusIndex();
    tObj["pan"] = track.pan();
    tObj["volume"] = track.volume();
    tObj["muted"] = track.isMuted();
    tObj["solo"] = track.isSolo();

    if (track.type() == Track::Type::Midi) {
        tObj["midiOutputDeviceId"] = track.midiOutputDeviceId();
        tObj["midiOutputDeviceName"] = track.midiOutputDeviceName();
        tObj["instrumentIndex"] = track.instrumentIndex();
    }

    QJsonArray eventsArr;
    for (const auto& event : track.events()) {
        QJsonObject eObj;
        if (event.clip())
            eObj["clipPath"] = event.clip()->filePath();
        eObj["startSample"] = static_cast<qint64>(event.startSample());
        eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
        eObj["durationSample"] = static_cast<qint64>(event.durationSample());
        eObj["sourceFrames"] = static_cast<qint64>(event.sourceFrames());
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

    if (track.type() == Track::Type::Midi) {
        QJsonArray midiEventsArr;
        for (const auto& event : track.midiEvents()) {
            QJsonObject eObj;
            eObj["startSample"] = static_cast<qint64>(event.startSample());
            eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
            eObj["durationSample"] = static_cast<qint64>(event.durationSample());
            if (event.clip())
                eObj["clip"] = event.clip()->toJson();
            if (!event.takes().empty()) {
                QJsonArray takesArr;
                for (const auto& take : event.takes())
                    takesArr.append(take->toJson());
                eObj["takes"] = takesArr;
                eObj["activeTakeIndex"] = event.activeTakeIndex();
            }
            midiEventsArr.append(eObj);
        }
        tObj["midiEvents"] = midiEventsArr;
    }

    if (track.pluginChain().count() > 0)
        tObj["plugins"] = track.pluginChain().toJson();
    return tObj;
}

static Track trackFromJson(const QJsonObject& tObj, PluginManager* manager = nullptr) {
    bool isMidi = tObj["type"].toString() == "midi";
    Track track(isMidi ? Track::Type::Midi : Track::Type::Audio,
                tObj["name"].toString());
    track.setChannels(tObj["channels"].toInt(2));
    track.setInputDeviceId(tObj["inputDeviceId"].toInt(-1));
    track.setInputChannel(tObj["inputChannel"].toInt(0));
    track.setOutputBusIndex(tObj["outputBusIndex"].toInt(0));
    track.setPan(static_cast<float>(tObj["pan"].toDouble(0.0)));
    track.setVolume(static_cast<float>(tObj["volume"].toDouble(vvvdaw::DefaultVolume)));
    track.setMuted(tObj["muted"].toBool(false));
    track.setSolo(tObj["solo"].toBool(false));

    if (isMidi) {
        track.setMidiOutputDeviceId(tObj["midiOutputDeviceId"].toInt(-1));
        track.setMidiOutputDeviceName(tObj["midiOutputDeviceName"].toString());
        track.setInstrumentIndex(tObj["instrumentIndex"].toInt(-1));
    }

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
        event.setSourceFrames(eObj.contains("sourceFrames")
            ? static_cast<int64_t>(eObj["sourceFrames"].toVariant().toLongLong())
            : event.durationSample());
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

    if (isMidi) {
        const QJsonArray midiEventsArr = tObj["midiEvents"].toArray();
        for (const auto& eVal : midiEventsArr) {
            QJsonObject eObj = eVal.toObject();
            MidiEvent event;
            if (eObj.contains("clip")) {
                auto clip = std::make_shared<MidiClip>();
                clip->fromJson(eObj["clip"].toObject());
                event.setClip(clip);
            }
            event.setStartSample(static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong()));
            event.setOffsetSample(static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong()));
            event.setDurationSample(static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong()));
            if (eObj.contains("takes")) {
                const QJsonArray takesArr = eObj["takes"].toArray();
                for (const auto& takeVal : takesArr) {
                    auto takeClip = std::make_shared<MidiClip>();
                    takeClip->fromJson(takeVal.toObject());
                    event.takes().push_back(takeClip);
                }
                event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
                if (event.activeTakeIndex() >= 0 && event.activeTakeIndex() < static_cast<int>(event.takes().size()))
                    event.setClip(event.takes()[event.activeTakeIndex()]);
            }
            track.addMidiEvent(event);
        }
    }

    if (tObj.contains("plugins"))
        track.pluginChain().fromJson(tObj["plugins"].toObject(), manager);
    return track;
}

// --- AddTrackCommand ---

AddTrackCommand::AddTrackCommand(Project& project, int index, int channels)
    : m_project(project), m_index(index), m_channels(channels), m_type(Track::Type::Audio) {}

AddTrackCommand::AddTrackCommand(Project& project, int index, Track::Type type)
    : m_project(project), m_index(index), m_channels(2), m_type(type) {}

void AddTrackCommand::execute() {
    if (m_type == Track::Type::Midi)
        m_project.addMidiTrack(QString());
    else
        m_project.addTrack(QString(), m_channels);
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

// --- SetTrackMonitorCommand ---

SetTrackMonitorCommand::SetTrackMonitorCommand(Project& project, int trackIndex, bool oldValue, bool newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackMonitorCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setMonitoring(m_newValue);
}

void SetTrackMonitorCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setMonitoring(m_oldValue);
}

// --- SetTrackArmCommand ---

SetTrackArmCommand::SetTrackArmCommand(Project& project, int trackIndex, bool oldValue, bool newValue)
    : m_project(project), m_trackIndex(trackIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTrackArmCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setRecordArmed(m_newValue);
}

void SetTrackArmCommand::undo() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].setRecordArmed(m_oldValue);
}

// --- SetTrackMidiOutputCommand ---

SetTrackMidiOutputCommand::SetTrackMidiOutputCommand(Project& project, int trackIndex,
                                                     Routing oldRouting, Routing newRouting)
    : m_project(project), m_trackIndex(trackIndex),
      m_oldRouting(oldRouting), m_newRouting(newRouting) {}

void SetTrackMidiOutputCommand::apply(const Routing& routing) {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto& track = m_project.tracks()[m_trackIndex];
    track.setMidiOutputDeviceId(routing.deviceId);
    track.setMidiOutputDeviceName(routing.deviceName);
    track.setInstrumentIndex(routing.instrumentIndex);
}

void SetTrackMidiOutputCommand::execute() { apply(m_newRouting); }
void SetTrackMidiOutputCommand::undo() { apply(m_oldRouting); }
