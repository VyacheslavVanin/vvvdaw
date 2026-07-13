#include "BusCommands.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include <QJsonObject>

static QJsonObject busToJson(const AudioBus& bus) {
    QJsonObject obj;
    obj["name"] = bus.name;
    obj["pan"] = bus.pan;
    obj["volume"] = bus.volume;
    obj["outputBusIndex"] = bus.outputBusIndex;
    obj["solo"] = bus.solo;
    obj["muted"] = bus.muted;
    obj["removable"] = bus.removable;
    return obj;
}

static AudioBus busFromJson(const QJsonObject& obj) {
    AudioBus bus;
    bus.name = obj["name"].toString();
    bus.pan = static_cast<float>(obj["pan"].toDouble(0.0));
    bus.volume = static_cast<float>(obj["volume"].toDouble(1.0));
    bus.outputBusIndex = obj["outputBusIndex"].toInt(0);
    bus.solo = obj["solo"].toBool(false);
    bus.muted = obj["muted"].toBool(false);
    bus.removable = obj["removable"].toBool(true);
    return bus;
}

// --- AddBusCommand ---

AddBusCommand::AddBusCommand(Project& project)
    : m_project(project) {}

void AddBusCommand::execute() {
    AudioBus newBus;
    newBus.name = QString("Bus %1").arg(m_project.buses().size());
    newBus.volume = 1.0f;
    newBus.pan = 0.0f;
    newBus.outputBusIndex = 0;
    m_addedIndex = m_project.addBus(std::move(newBus));
}

void AddBusCommand::undo() {
    m_project.removeBus(m_addedIndex);
}

// --- RemoveBusCommand ---

RemoveBusCommand::RemoveBusCommand(Project& project, int index)
    : m_project(project), m_index(index) {
    if (index >= 0 && index < static_cast<int>(m_project.buses().size()))
        m_savedBus = busToJson(m_project.buses()[index]);
}

void RemoveBusCommand::execute() {
    m_project.removeBus(m_index);
}

void RemoveBusCommand::undo() {
    AudioBus bus = busFromJson(m_savedBus);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.buses().size())) {
        m_project.buses().insert(m_project.buses().begin() + m_index, std::move(bus));
    } else {
        m_project.addBus(std::move(bus));
    }
}

// --- SetBusVolumeCommand ---

SetBusVolumeCommand::SetBusVolumeCommand(Project& project, int busIndex, float oldValue, float newValue)
    : m_project(project), m_busIndex(busIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetBusVolumeCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].volume = m_newValue;
}

void SetBusVolumeCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].volume = m_oldValue;
}

bool SetBusVolumeCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetBusVolumeCommand*>(other);
    if (m_busIndex != cmd->m_busIndex) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetBusPanCommand ---

SetBusPanCommand::SetBusPanCommand(Project& project, int busIndex, float oldValue, float newValue)
    : m_project(project), m_busIndex(busIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetBusPanCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].pan = m_newValue;
}

void SetBusPanCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].pan = m_oldValue;
}

bool SetBusPanCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetBusPanCommand*>(other);
    if (m_busIndex != cmd->m_busIndex) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetBusMuteCommand ---

SetBusMuteCommand::SetBusMuteCommand(Project& project, int busIndex, bool oldValue, bool newValue)
    : m_project(project), m_busIndex(busIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetBusMuteCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].muted = m_newValue;
}

void SetBusMuteCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].muted = m_oldValue;
}

// --- SetBusSoloCommand ---

SetBusSoloCommand::SetBusSoloCommand(Project& project, int busIndex, bool oldValue, bool newValue)
    : m_project(project), m_busIndex(busIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetBusSoloCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].solo = m_newValue;
}

void SetBusSoloCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].solo = m_oldValue;
}

// --- SetBusNameCommand ---

SetBusNameCommand::SetBusNameCommand(Project& project, int busIndex, const QString& oldName, const QString& newName)
    : m_project(project), m_busIndex(busIndex), m_oldName(oldName), m_newName(newName) {}

void SetBusNameCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].name = m_newName;
}

void SetBusNameCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].name = m_oldName;
}

// --- SetBusOutputCommand ---

SetBusOutputCommand::SetBusOutputCommand(Project& project, int busIndex, int oldValue, int newValue)
    : m_project(project), m_busIndex(busIndex), m_oldValue(oldValue), m_newValue(newValue) {}

void SetBusOutputCommand::execute() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].outputBusIndex = m_newValue;
}

void SetBusOutputCommand::undo() {
    if (m_busIndex >= 0 && m_busIndex < static_cast<int>(m_project.buses().size()))
        m_project.buses()[m_busIndex].outputBusIndex = m_oldValue;
}
