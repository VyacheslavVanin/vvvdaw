#pragma once
#include "core/UndoCommand.h"
#include "model/AudioBus.h"
#include "SetValueCommand.h"
#include <QJsonObject>
#include <QString>
#include <vector>

class Project;

class AddBusCommand : public UndoCommand {
public:
    AddBusCommand(Project& project);
    void execute() override;
    void undo() override;
    int id() const override { return 20; }
private:
    Project& m_project;
    int m_addedIndex = -1;
};

class RemoveBusCommand : public UndoCommand {
public:
    RemoveBusCommand(Project& project, int index);
    void execute() override;
    void undo() override;
    int id() const override { return 21; }
private:
    Project& m_project;
    int m_index;
    QJsonObject m_savedBus;
};

using SetBusVolumeCommand = vvvcmd::SetValueCommand<
    AudioBus, float, 22, true, true, &AudioBus::setVolume>;
using SetBusPanCommand = vvvcmd::SetValueCommand<
    AudioBus, float, 23, true, true, &AudioBus::setPan>;
using SetBusMuteCommand = vvvcmd::SetValueCommand<
    AudioBus, bool, 24, false, true, &AudioBus::setMuted>;
using SetBusSoloCommand = vvvcmd::SetValueCommand<
    AudioBus, bool, 25, false, true, &AudioBus::setSolo>;
using SetBusNameCommand = vvvcmd::SetValueCommand<
    AudioBus, QString, 26, false, true, &AudioBus::setName>;
using SetBusOutputCommand = vvvcmd::SetValueCommand<
    AudioBus, int, 27, false, true, &AudioBus::setOutputBusIndex>;

// --- Bus sends (extra outputs splitting the signal into other buses) ---

class AddBusSendCommand : public UndoCommand {
public:
    AddBusSendCommand(Project& project, int busIndex);
    void execute() override;
    void undo() override;
    int id() const override { return 90; }
private:
    Project& m_project;
    int m_busIndex;
    int m_addedIndex = -1;
};

class RemoveBusSendCommand : public UndoCommand {
public:
    RemoveBusSendCommand(Project& project, int busIndex, int sendIndex);
    void execute() override;
    void undo() override;
    int id() const override { return 91; }
private:
    Project& m_project;
    int m_busIndex;
    int m_sendIndex;
    AudioBus::Send m_savedSend;
};

// Undoable "set one scalar property on a bus send". Set is a pointer to the
// mutating member function of AudioBus::Send (e.g. &AudioBus::Send::setLevel).
// oldValue/newValue are captured at construction.
template <class Value, int CommandId, bool Mergeable, auto Set>
class SetBusSendCommand : public UndoCommand {
public:
    SetBusSendCommand(Project& project, int busIndex, int sendIndex,
                      Value oldValue, Value newValue)
        : m_project(project), m_busIndex(busIndex), m_sendIndex(sendIndex),
          m_oldValue(oldValue), m_newValue(newValue) {}

    void execute() override {
        if (auto* send = targetSend())
            (send->*Set)(m_newValue);
    }

    void undo() override {
        if (auto* send = targetSend())
            (send->*Set)(m_oldValue);
    }

    int id() const override { return CommandId; }

    bool mergeWith(const UndoCommand* other) override {
        if constexpr (!Mergeable)
            return false;
        auto* cmd = static_cast<const SetBusSendCommand*>(other);
        if (m_busIndex != cmd->m_busIndex || m_sendIndex != cmd->m_sendIndex)
            return false;
        m_newValue = cmd->m_newValue;
        return true;
    }

private:
    AudioBus::Send* targetSend() const {
        if (auto* bus = m_project.busAt(m_busIndex)) {
            if (m_sendIndex >= 0 &&
                m_sendIndex < static_cast<int>(bus->sends().size())) {
                return &bus->sends()[static_cast<size_t>(m_sendIndex)];
            }
        }
        return nullptr;
    }

    Project& m_project;
    int m_busIndex;
    int m_sendIndex;
    Value m_oldValue;
    Value m_newValue;
};

using SetBusSendTargetCommand = SetBusSendCommand<
    int, 92, false, &AudioBus::Send::setBus>;
using SetBusSendLevelCommand = SetBusSendCommand<
    float, 93, true, &AudioBus::Send::setLevel>;
using SetBusSendPreCommand = SetBusSendCommand<
    bool, 94, false, &AudioBus::Send::setPreFader>;

// --- Bus folders / ordering ---

class ReorderBusesCommand : public UndoCommand {
public:
    ReorderBusesCommand(Project& project, std::vector<int> oldOrder,
                        std::vector<int> newOrder);
    void execute() override;
    void undo() override;
    int id() const override { return 95; }
private:
    Project& m_project;
    std::vector<int> m_oldOrder;
    std::vector<int> m_newOrder;
};

// Atomically re-route buses into/out of folders and reorder the panel.
// Captures the old/new display order and the old/new parent (outputBusIndex)
// per affected bus for undo/redo.
class MoveBusesCommand : public UndoCommand {
public:
    MoveBusesCommand(Project& project, std::vector<int> oldOrder,
                     std::vector<int> newOrder,
                     std::vector<std::pair<int, int>> oldParents,
                     std::vector<std::pair<int, int>> newParents);
    void execute() override;
    void undo() override;
    int id() const override { return 96; }
private:
    Project& m_project;
    std::vector<int> m_oldOrder;
    std::vector<int> m_newOrder;
    std::vector<std::pair<int, int>> m_oldParents;
    std::vector<std::pair<int, int>> m_newParents;
};

class SetBusFolderCollapsedCommand : public UndoCommand {
public:
    SetBusFolderCollapsedCommand(Project& project, int busIndex,
                                 bool oldVal, bool newVal);
    void execute() override;
    void undo() override;
    int id() const override { return 97; }
private:
    Project& m_project;
    int m_busIndex;
    bool m_oldVal;
    bool m_newVal;
};

// Create a new folder bus and route the given children into it. The folder is
// appended (highest index); undo restores the children's original parents and
// removes the folder.
class CreateBusFolderCommand : public UndoCommand {
public:
    CreateBusFolderCommand(Project& project, QString folderName,
                           std::vector<int> children);
    void execute() override;
    void undo() override;
    int id() const override { return 98; }
private:
    Project& m_project;
    QString m_name;
    std::vector<int> m_children;
    std::vector<std::pair<int, int>> m_oldParents;
    int m_folderIndex = -1;
};
