#pragma once
#include "core/UndoCommand.h"
#include "model/AudioBus.h"
#include "SetValueCommand.h"
#include <QJsonObject>
#include <QString>

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
