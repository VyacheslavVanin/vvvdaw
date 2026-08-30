#pragma once
#include "core/UndoCommand.h"
#include "model/Track.h"
#include "SetValueCommand.h"
#include <QString>
#include <QJsonObject>
#include <vector>

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

using SetTrackVolumeCommand = vvvcmd::SetValueCommand<
    Track, float, 10, true, false, &Track::setVolume>;
using SetTrackPanCommand = vvvcmd::SetValueCommand<
    Track, float, 11, true, false, &Track::setPan>;
using SetTrackMuteCommand = vvvcmd::SetValueCommand<
    Track, bool, 12, false, false, &Track::setMuted>;
using SetTrackSoloCommand = vvvcmd::SetValueCommand<
    Track, bool, 13, false, false, &Track::setSolo>;
using SetTrackOutputCommand = vvvcmd::SetValueCommand<
    Track, int, 14, false, false, &Track::setOutputBusIndex>;
using SetTrackMonitorCommand = vvvcmd::SetValueCommand<
    Track, bool, 15, false, false, &Track::setMonitoring>;
using SetTrackArmCommand = vvvcmd::SetValueCommand<
    Track, bool, 16, false, false, &Track::setRecordArmed>;
using SetTrackHeightCommand = vvvcmd::SetValueCommand<
    Track, int, 18, false, false, &Track::setHeight>;

// Apply one height to every track (the Shift-drag "resize all" gesture).
// Undo restores each track's previous height.
class SetAllTracksHeightCommand : public UndoCommand {
public:
    SetAllTracksHeightCommand(Project& project, std::vector<int> oldHeights, int newHeight);
    void execute() override;
    void undo() override;
    int id() const override { return 130; }
private:
    Project& m_project;
    std::vector<int> m_oldHeights;
    int m_newHeight;
};

// Move the tracks vector to a new ordering. `newOrder` is a permutation of
// 0..n-1 where position `i` in the new order is the track that was at
// `newOrder[i]` before the move. The inverse order is derived for undo.
class ReorderTracksCommand : public UndoCommand {
public:
    ReorderTracksCommand(Project& project, std::vector<int> newOrder);
    void execute() override;
    void undo() override;
    int id() const override { return 131; }
private:
    void applyOrder(const std::vector<int>& order);
    Project& m_project;
    std::vector<int> m_oldOrder;
    std::vector<int> m_newOrder;
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
