#pragma once
#include "core/UndoCommand.h"
#include <QString>
#include <QJsonObject>

class Project;
class PluginManager;

class AddTrackCommand : public UndoCommand {
public:
    AddTrackCommand(Project& project, int index);
    void execute() override;
    void undo() override;
    int id() const override { return 1; }
private:
    Project& m_project;
    int m_index;
};

class RemoveTrackCommand : public UndoCommand {
public:
    RemoveTrackCommand(Project& project, int index, PluginManager* manager = nullptr);
    void execute() override;
    void undo() override;
    int id() const override { return 2; }
private:
    Project& m_project;
    int m_index;
    QJsonObject m_savedTrack;
    int m_savedIndex = -1;
    PluginManager* m_manager = nullptr;
};

class SetTrackVolumeCommand : public UndoCommand {
public:
    SetTrackVolumeCommand(Project& project, int trackIndex, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 10; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_trackIndex;
    float m_oldValue;
    float m_newValue;
};

class SetTrackPanCommand : public UndoCommand {
public:
    SetTrackPanCommand(Project& project, int trackIndex, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 11; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_trackIndex;
    float m_oldValue;
    float m_newValue;
};

class SetTrackMuteCommand : public UndoCommand {
public:
    SetTrackMuteCommand(Project& project, int trackIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 12; }
private:
    Project& m_project;
    int m_trackIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetTrackSoloCommand : public UndoCommand {
public:
    SetTrackSoloCommand(Project& project, int trackIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 13; }
private:
    Project& m_project;
    int m_trackIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetTrackOutputCommand : public UndoCommand {
public:
    SetTrackOutputCommand(Project& project, int trackIndex, int oldValue, int newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 14; }
private:
    Project& m_project;
    int m_trackIndex;
    int m_oldValue;
    int m_newValue;
};
