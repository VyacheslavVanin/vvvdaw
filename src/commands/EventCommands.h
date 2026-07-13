#pragma once
#include "core/UndoCommand.h"
#include <QJsonObject>
#include <cstdint>

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
