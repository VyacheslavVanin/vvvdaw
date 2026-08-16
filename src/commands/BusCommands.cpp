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

// --- AddBusSendCommand ---

AddBusSendCommand::AddBusSendCommand(Project& project, int busIndex)
    : m_project(project), m_busIndex(busIndex) {}

void AddBusSendCommand::execute() {
    AudioBus* bus = m_project.busAt(m_busIndex);
    if (!bus) return;
    AudioBus::Send send;
    send.busIndex = 0;
    send.level = 1.0f;
    send.preFader = false;
    bus->sends().push_back(send);
    m_addedIndex = static_cast<int>(bus->sends().size()) - 1;
}

void AddBusSendCommand::undo() {
    AudioBus* bus = m_project.busAt(m_busIndex);
    if (!bus) return;
    if (m_addedIndex >= 0 && m_addedIndex < static_cast<int>(bus->sends().size()))
        bus->sends().erase(bus->sends().begin() + m_addedIndex);
}

// --- RemoveBusSendCommand ---

RemoveBusSendCommand::RemoveBusSendCommand(Project& project, int busIndex, int sendIndex)
    : m_project(project), m_busIndex(busIndex), m_sendIndex(sendIndex) {
    if (const AudioBus* bus = m_project.busAt(busIndex)) {
        if (sendIndex >= 0 && sendIndex < static_cast<int>(bus->sends().size()))
            m_savedSend = bus->sends()[static_cast<size_t>(sendIndex)];
    }
}

void RemoveBusSendCommand::execute() {
    AudioBus* bus = m_project.busAt(m_busIndex);
    if (!bus) return;
    if (m_sendIndex >= 0 && m_sendIndex < static_cast<int>(bus->sends().size()))
        bus->sends().erase(bus->sends().begin() + m_sendIndex);
}

void RemoveBusSendCommand::undo() {
    AudioBus* bus = m_project.busAt(m_busIndex);
    if (!bus) return;
    if (m_sendIndex >= 0 && m_sendIndex <= static_cast<int>(bus->sends().size()))
        bus->sends().insert(bus->sends().begin() + m_sendIndex, m_savedSend);
}
