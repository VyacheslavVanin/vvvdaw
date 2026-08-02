#pragma once
#include "core/UndoCommand.h"
#include "model/Track.h"
#include <QString>
#include <QJsonObject>

class Project;
class PluginManager;

class AddTrackCommand : public UndoCommand {
public:
    AddTrackCommand(Project& project, int index, int channels = 2);
    AddTrackCommand(Project& project, int index, Track::Type type);
    void execute() override;
    void undo() override;
    int id() const override { return 1; }
private:
    Project& m_project;
    int m_index;
    int m_channels;
    Track::Type m_type;
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
    bool requiresPluginWindowsClose() const override { return false; }
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
    bool requiresPluginWindowsClose() const override { return false; }
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
    bool requiresPluginWindowsClose() const override { return false; }
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
    bool requiresPluginWindowsClose() const override { return false; }
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
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int m_oldValue;
    int m_newValue;
};

class SetTrackMonitorCommand : public UndoCommand {
public:
    SetTrackMonitorCommand(Project& project, int trackIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 15; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetTrackArmCommand : public UndoCommand {
public:
    SetTrackArmCommand(Project& project, int trackIndex, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 16; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    bool m_oldValue;
    bool m_newValue;
};

class SetTrackMidiOutputCommand : public UndoCommand {
public:
    struct Routing {
        int deviceId = -1;
        QString deviceName;
        int instrumentIndex = -1;
    };
    SetTrackMidiOutputCommand(Project& project, int trackIndex, Routing oldRouting, Routing newRouting);
    void execute() override;
    void undo() override;
    int id() const override { return 17; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    void apply(const Routing& routing);
    Project& m_project;
    int m_trackIndex;
    Routing m_oldRouting;
    Routing m_newRouting;
};
