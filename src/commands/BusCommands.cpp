#include "BusCommands.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include <QJsonObject>
#include <utility>

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

// --- ReorderBusesCommand ---

ReorderBusesCommand::ReorderBusesCommand(Project& project,
                                         std::vector<int> oldOrder,
                                         std::vector<int> newOrder)
    : m_project(project),
      m_oldOrder(std::move(oldOrder)),
      m_newOrder(std::move(newOrder)) {}

void ReorderBusesCommand::execute() {
    m_project.setBusDisplayOrder(m_newOrder);
}

void ReorderBusesCommand::undo() {
    m_project.setBusDisplayOrder(m_oldOrder);
}

// --- MoveBusesCommand ---

MoveBusesCommand::MoveBusesCommand(Project& project,
                                   std::vector<int> oldOrder,
                                   std::vector<int> newOrder,
                                   std::vector<std::pair<int, int>> oldParents,
                                   std::vector<std::pair<int, int>> newParents)
    : m_project(project),
      m_oldOrder(std::move(oldOrder)),
      m_newOrder(std::move(newOrder)),
      m_oldParents(std::move(oldParents)),
      m_newParents(std::move(newParents)) {}

void MoveBusesCommand::execute() {
    m_project.setBusDisplayOrder(m_newOrder);
    for (const auto& [idx, parent] : m_newParents)
        if (AudioBus* bus = m_project.busAt(idx))
            bus->setOutputBusIndex(parent);
}

void MoveBusesCommand::undo() {
    m_project.setBusDisplayOrder(m_oldOrder);
    for (const auto& [idx, parent] : m_oldParents)
        if (AudioBus* bus = m_project.busAt(idx))
            bus->setOutputBusIndex(parent);
}

// --- SetBusFolderCollapsedCommand ---

SetBusFolderCollapsedCommand::SetBusFolderCollapsedCommand(Project& project,
                                                           int busIndex,
                                                           bool oldVal,
                                                           bool newVal)
    : m_project(project), m_busIndex(busIndex),
      m_oldVal(oldVal), m_newVal(newVal) {}

void SetBusFolderCollapsedCommand::execute() {
    if (AudioBus* bus = m_project.busAt(m_busIndex))
        bus->setFolderCollapsed(m_newVal);
}

void SetBusFolderCollapsedCommand::undo() {
    if (AudioBus* bus = m_project.busAt(m_busIndex))
        bus->setFolderCollapsed(m_oldVal);
}

// --- CreateBusFolderCommand ---

CreateBusFolderCommand::CreateBusFolderCommand(Project& project,
                                               QString folderName,
                                               std::vector<int> children)
    : m_project(project), m_name(std::move(folderName)), m_children(std::move(children)) {
    for (int c : m_children)
        if (const AudioBus* bus = m_project.busAt(c))
            m_oldParents.emplace_back(c, bus->outputBusIndex());
}

void CreateBusFolderCommand::execute() {
    if (m_folderIndex >= 0) return; // redo
    AudioBus folder;
    folder.setName(m_name);
    folder.setVolume(1.0f);
    folder.setOutputBusIndex(0);
    m_folderIndex = m_project.addBus(std::move(folder));
    for (int c : m_children)
        if (AudioBus* bus = m_project.busAt(c))
            bus->setOutputBusIndex(m_folderIndex);
}

void CreateBusFolderCommand::undo() {
    if (m_folderIndex < 0) return;
    for (const auto& [c, parent] : m_oldParents)
        if (AudioBus* bus = m_project.busAt(c))
            bus->setOutputBusIndex(parent);
    m_project.removeBus(m_folderIndex);
    m_folderIndex = -1;
}
