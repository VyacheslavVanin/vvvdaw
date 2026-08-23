#pragma once
#include "core/UndoCommand.h"
#include "model/AudioClip.h"
#include <QJsonObject>
#include <cstdint>
#include <memory>
#include <vector>

class Project;

class AddEventCommand : public UndoCommand {
public:
    AddEventCommand(Project& project, int trackIndex, QJsonObject eventJson);
    void execute() override;
    void undo() override;
    int id() const override { return 40; }
private:
    Project& m_project;
    int m_trackIndex;
    QJsonObject m_eventJson;
};

class RemoveEventCommand : public UndoCommand {
public:
    RemoveEventCommand(Project& project, int trackIndex, int64_t eventId);
    void execute() override;
    void undo() override;
    int id() const override { return 41; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    QJsonObject m_savedEvent;
};

class MoveEventCommand : public UndoCommand {
public:
    MoveEventCommand(Project& project, int trackIndex, int64_t eventId, int64_t oldStart, int64_t newStart);
    void execute() override;
    void undo() override;
    int id() const override { return 42; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_oldStart;
    int64_t m_newStart;
};

class TrimEventCommand : public UndoCommand {
public:
    TrimEventCommand(Project& project, int trackIndex, int64_t eventId,
                     int64_t oldStart, int64_t newStart,
                     int64_t oldOffset, int64_t oldDuration,
                     int64_t newOffset, int64_t newDuration);
    void execute() override;
    void undo() override;
    int id() const override { return 43; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_oldStart, m_newStart;
    int64_t m_oldOffset, m_oldDuration;
    int64_t m_newOffset, m_newDuration;
};

class SwitchTakeCommand : public UndoCommand {
public:
    SwitchTakeCommand(Project& project, int trackIndex, int64_t eventId, int oldTake, int newTake);
    void execute() override;
    void undo() override;
    int id() const override { return 44; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int m_oldTake;
    int m_newTake;
};

// Split an audio event in two at `cutSample` (exact, no grid snap). Both parts
// reference the same source clip; placed flush together they reproduce the
// original event. The original event becomes the left part.
//
// With `snapToGrid` the two pieces are aligned to the nearest snap position
// (`snapSample(cutSample, snapUnit)`): when the cut lands after a grid line the
// left piece is trimmed to the line and the right piece slides left to meet it
// (audio between the line and the cut is dropped); when it lands before a grid
// line only the right piece slides forward to the line (leaving a gap).
class CutEventCommand : public UndoCommand {
public:
    CutEventCommand(Project& project, int trackIndex, int64_t eventId, int64_t cutSample,
                    bool snapToGrid = false, double snapUnit = 0.0);
    void execute() override;
    void undo() override;
    int id() const override { return 45; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_cutSample;
    bool m_snapToGrid;
    double m_snapUnit;

    // Original event state, saved so undo restores it faithfully (including
    // in-memory clips that cannot round-trip through JSON).
    int64_t m_savedStart = 0;
    int64_t m_savedOffset = 0;
    int64_t m_savedDuration = 0;
    int64_t m_savedSourceFrames = 0;
    int64_t m_savedFadeIn = 0;
    int64_t m_savedFadeOut = 0;
    std::shared_ptr<AudioClip> m_savedClip;
    std::vector<std::shared_ptr<AudioClip>> m_savedTakes;
    int m_savedActiveTakeIndex = -1;

    int64_t m_rightEventId = -1;
    bool m_didCut = false;
};

// Set fade-in / fade-out lengths (in samples) on the junctions between the
// given audio events, sorted by their timeline start position. For each
// consecutive pair the left event gets a fade-out and the right event a
// fade-in of `fadeSamples`, so the shared boundary crossfades and hides the
// discontinuity. `fadeSamples` of 0 removes the fades. Fewer than two events
// is a no-op.
class SetEventsFadeCommand : public UndoCommand {
public:
    SetEventsFadeCommand(Project& project, int trackIndex,
                         std::vector<int64_t> eventIds, int64_t fadeSamples);
    void execute() override;
    void undo() override;
    int id() const override { return 47; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    struct FadeState {
        int64_t oldFadeIn = 0;
        int64_t oldFadeOut = 0;
    };
    Project& m_project;
    int m_trackIndex;
    std::vector<int64_t> m_eventIds;
    int64_t m_fadeSamples;
    std::vector<FadeState> m_oldStates;
};

// Move an event from one track to another. The move already happened live
// (drag release); the command records the before/after so undo relocates it
// back and redo reapplies it. Both parts keep the same event id.
class MoveEventToTrackCommand : public UndoCommand {
public:
    MoveEventToTrackCommand(Project& project, int srcTrackIndex, int dstTrackIndex,
                            int64_t eventId, int64_t oldStart, int64_t newStart);
    void execute() override;
    void undo() override;
    int id() const override { return 46; }
private:
    Project& m_project;
    int m_srcTrackIndex;
    int m_dstTrackIndex;
    int64_t m_eventId;
    int64_t m_oldStart;
    int64_t m_newStart;
};
