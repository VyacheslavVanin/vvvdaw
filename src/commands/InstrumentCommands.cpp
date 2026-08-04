#include "InstrumentCommands.h"
#include "model/Project.h"
#include "model/Instrument.h"
#include "model/Track.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginManager.h"
#include <QJsonObject>

static QJsonObject instrumentToJson(const Instrument& instrument) {
    QJsonObject obj;
    obj["name"] = instrument.name();
    obj["pan"] = instrument.pan();
    obj["volume"] = instrument.volume();
    obj["outputBusIndex"] = instrument.outputBusIndex();
    obj["solo"] = instrument.isSolo();
    obj["muted"] = instrument.isMuted();
    if (instrument.synth())
        obj["synth"] = instrument.synth()->stateToJson();
    if (instrument.effects().count() > 0)
        obj["effects"] = instrument.effects().toJson();
    return obj;
}

static Instrument instrumentFromJson(const QJsonObject& obj, PluginManager* manager = nullptr) {
    Instrument instrument;
    instrument.setName(obj["name"].toString("Instrument"));
    instrument.setPan(static_cast<float>(obj["pan"].toDouble(0.0)));
    instrument.setVolume(static_cast<float>(obj["volume"].toDouble(1.0)));
    instrument.setOutputBusIndex(obj["outputBusIndex"].toInt(0));
    instrument.setSolo(obj["solo"].toBool(false));
    instrument.setMuted(obj["muted"].toBool(false));
    if (obj.contains("synth")) {
        auto synth = PluginChain::createInstance(obj["synth"].toObject(), manager);
        if (synth)
            instrument.setSynth(std::move(synth));
    }
    if (obj.contains("effects"))
        instrument.effects().fromJson(obj["effects"].toObject(), manager);
    return instrument;
}

// --- AddInstrumentCommand ---

AddInstrumentCommand::AddInstrumentCommand(Project& project)
    : m_project(project) {}

void AddInstrumentCommand::execute() {
    Instrument inst;
    inst.setName(QString("Instrument %1").arg(m_project.instruments().size() + 1));
    m_addedIndex = m_project.addInstrument(std::move(inst));
}

void AddInstrumentCommand::undo() {
    m_project.removeInstrument(m_addedIndex);
}

// --- RemoveInstrumentCommand ---

RemoveInstrumentCommand::RemoveInstrumentCommand(Project& project, int index,
                                                 PluginManager* manager,
                                                 double sampleRate, int bufferSize)
    : m_project(project), m_index(index), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {
    if (index >= 0 && index < static_cast<int>(m_project.instruments().size()))
        m_savedInstrument = instrumentToJson(m_project.instruments()[index]);
}

void RemoveInstrumentCommand::execute() {
    m_project.removeInstrument(m_index);
}

void RemoveInstrumentCommand::undo() {
    Instrument inst = instrumentFromJson(m_savedInstrument, m_manager);
    if (inst.synth())
        inst.synth()->activate(m_sampleRate, m_bufferSize);
    inst.effects().activate(m_sampleRate, m_bufferSize);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.instruments().size()))
        m_project.instruments().insert(m_project.instruments().begin() + m_index, std::move(inst));
    else
        m_project.addInstrument(std::move(inst));
}

// --- SetInstrumentVolumeCommand ---

SetInstrumentVolumeCommand::SetInstrumentVolumeCommand(Project& project, int index,
                                                       float oldValue, float newValue)
    : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

void SetInstrumentVolumeCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setVolume(m_newValue);
}

void SetInstrumentVolumeCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setVolume(m_oldValue);
}

bool SetInstrumentVolumeCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetInstrumentVolumeCommand*>(other);
    if (m_index != cmd->m_index) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetInstrumentPanCommand ---

SetInstrumentPanCommand::SetInstrumentPanCommand(Project& project, int index,
                                                 float oldValue, float newValue)
    : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

void SetInstrumentPanCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setPan(m_newValue);
}

void SetInstrumentPanCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setPan(m_oldValue);
}

bool SetInstrumentPanCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetInstrumentPanCommand*>(other);
    if (m_index != cmd->m_index) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetInstrumentMuteCommand ---

SetInstrumentMuteCommand::SetInstrumentMuteCommand(Project& project, int index,
                                                   bool oldValue, bool newValue)
    : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

void SetInstrumentMuteCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setMuted(m_newValue);
}

void SetInstrumentMuteCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setMuted(m_oldValue);
}

// --- SetInstrumentSoloCommand ---

SetInstrumentSoloCommand::SetInstrumentSoloCommand(Project& project, int index,
                                                   bool oldValue, bool newValue)
    : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

void SetInstrumentSoloCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setSolo(m_newValue);
}

void SetInstrumentSoloCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setSolo(m_oldValue);
}

// --- SetInstrumentOutputCommand ---

SetInstrumentOutputCommand::SetInstrumentOutputCommand(Project& project, int index,
                                                       int oldValue, int newValue)
    : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

void SetInstrumentOutputCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setOutputBusIndex(m_newValue);
}

void SetInstrumentOutputCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setOutputBusIndex(m_oldValue);
}

// --- SetInstrumentNameCommand ---

SetInstrumentNameCommand::SetInstrumentNameCommand(Project& project, int index,
                                                   const QString& oldName, const QString& newName)
    : m_project(project), m_index(index), m_oldName(oldName), m_newName(newName) {}

void SetInstrumentNameCommand::execute() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setName(m_newName);
}

void SetInstrumentNameCommand::undo() {
    if (m_index >= 0 && m_index < static_cast<int>(m_project.instruments().size()))
        m_project.instruments()[m_index].setName(m_oldName);
}

// --- SetInstrumentSynthCommand ---

SetInstrumentSynthCommand::SetInstrumentSynthCommand(Project& project, int index,
                                                     QJsonObject oldSynthJson, QJsonObject newSynthJson,
                                                     PluginManager* manager,
                                                     double sampleRate, int bufferSize)
    : m_project(project), m_index(index),
      m_oldSynthJson(oldSynthJson), m_newSynthJson(newSynthJson), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {}

void SetInstrumentSynthCommand::execute() {
    if (m_index < 0 || m_index >= static_cast<int>(m_project.instruments().size()))
        return;
    auto synth = PluginChain::createInstance(m_newSynthJson, m_manager);
    if (synth) synth->activate(m_sampleRate, m_bufferSize);
    m_project.instruments()[m_index].setSynth(std::move(synth));
}

void SetInstrumentSynthCommand::undo() {
    if (m_index < 0 || m_index >= static_cast<int>(m_project.instruments().size()))
        return;
    auto synth = PluginChain::createInstance(m_oldSynthJson, m_manager);
    if (synth) synth->activate(m_sampleRate, m_bufferSize);
    m_project.instruments()[m_index].setSynth(std::move(synth));
}
