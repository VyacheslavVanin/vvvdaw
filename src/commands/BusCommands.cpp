#include "BusCommands.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include <QJsonObject>

// --- AddBusCommand ---

AddBusCommand::AddBusCommand(Project& project)
    : m_project(project) {}

void AddBusCommand::execute() {
    AudioBus newBus;
    newBus.setName(QString("Bus %1").arg(m_project.buses().size()));
    newBus.setVolume(1.0f);
    newBus.setPan(0.0f);
    newBus.setOutputBusIndex(0);
    m_addedIndex = m_project.addBus(std::move(newBus));
}

void AddBusCommand::undo() {
    m_project.removeBus(m_addedIndex);
}

// --- RemoveBusCommand ---

RemoveBusCommand::RemoveBusCommand(Project& project, int index)
    : m_project(project), m_index(index) {
    if (const AudioBus* bus = m_project.busAt(index))
        m_savedBus = bus->toJson();
}

void RemoveBusCommand::execute() {
    m_project.removeBus(m_index);
}

void RemoveBusCommand::undo() {
    AudioBus bus = AudioBus::fromJson(m_savedBus);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.buses().size())) {
        m_project.buses().insert(m_project.buses().begin() + m_index, std::move(bus));
    } else {
        m_project.addBus(std::move(bus));
    }
}
