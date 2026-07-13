#pragma once
#include "core/UndoCommand.h"
#include <QJsonObject>

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

class SetBusVolumeCommand : public UndoCommand {
public:
    SetBusVolumeCommand(Project& project, int busIndex, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 22; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_busIndex;
    float m_oldValue;
    float m_newValue;
};

class SetBusPanCommand : public UndoCommand {
public:
    SetBusPanCommand(Project& project, int busIndex, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 23; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_busIndex;
    float m_oldValue;
    float m_newValue;
};
