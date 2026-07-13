#pragma once
#include "core/UndoCommand.h"
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

class SetBusMuteCommand : public UndoCommand {
public:
    SetBusMuteCommand(Project& project, int busIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 24; }
private:
    Project& m_project;
    int m_busIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetBusSoloCommand : public UndoCommand {
public:
    SetBusSoloCommand(Project& project, int busIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 25; }
private:
    Project& m_project;
    int m_busIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetBusNameCommand : public UndoCommand {
public:
    SetBusNameCommand(Project& project, int busIndex, const QString& oldName, const QString& newName);
    void execute() override;
    void undo() override;
    int id() const override { return 26; }
private:
    Project& m_project;
    int m_busIndex;
    QString m_oldName;
    QString m_newName;
};

class SetBusOutputCommand : public UndoCommand {
public:
    SetBusOutputCommand(Project& project, int busIndex, int oldValue, int newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 27; }
private:
    Project& m_project;
    int m_busIndex;
    int m_oldValue;
    int m_newValue;
};
