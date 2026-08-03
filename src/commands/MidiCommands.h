#pragma once
#include "core/UndoCommand.h"
#include <QJsonObject>
#include <QJsonArray>
#include <cstdint>
#include <vector>

class Project;

class AddMidiEventCommand : public UndoCommand {
public:
    AddMidiEventCommand(Project& project, int trackIndex, QJsonObject eventJson);
    void execute() override;
    void undo() override;
    int id() const override { return 50; }
    bool requiresPluginWindowsClose() const override { return false; }
    int64_t createdEventId() const { return m_createdEventId; }
private:
    Project& m_project;
    int m_trackIndex;
    QJsonObject m_eventJson;
    int64_t m_createdEventId = -1;
};

class RemoveMidiEventCommand : public UndoCommand {
public:
    RemoveMidiEventCommand(Project& project, int trackIndex, int64_t eventId);
    void execute() override;
    void undo() override;
    int id() const override { return 51; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    QJsonObject m_savedEvent;
};

class MoveMidiEventCommand : public UndoCommand {
public:
    MoveMidiEventCommand(Project& project, int trackIndex, int64_t eventId,
                         int64_t oldStart, int64_t newStart);
    void execute() override;
    void undo() override;
    int id() const override { return 52; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_oldStart;
    int64_t m_newStart;
};

class TrimMidiEventCommand : public UndoCommand {
public:
    TrimMidiEventCommand(Project& project, int trackIndex, int64_t eventId,
                         int64_t oldOffset, int64_t oldDuration,
                         int64_t newOffset, int64_t newDuration);
    void execute() override;
    void undo() override;
    int id() const override { return 53; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_oldOffset, m_oldDuration;
    int64_t m_newOffset, m_newDuration;
};

class AddNoteCommand : public UndoCommand {
public:
    AddNoteCommand(Project& project, int trackIndex, int64_t eventId,
                   int pitch, int velocity, int64_t startTick, int64_t durationTicks);
    void execute() override;
    void undo() override;
    int id() const override { return 60; }
    int64_t createdNoteId() const { return m_createdNoteId; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int m_pitch;
    int m_velocity;
    int64_t m_startTick;
    int64_t m_durationTicks;
    int64_t m_createdNoteId = -1;
};

class RemoveNoteCommand : public UndoCommand {
public:
    RemoveNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId);
    void execute() override;
    void undo() override;
    int id() const override { return 61; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_noteId;
    QJsonObject m_savedNote;
};

class MoveNoteCommand : public UndoCommand {
public:
    MoveNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId,
                    int oldPitch, int64_t oldStartTick,
                    int newPitch, int64_t newStartTick);
    void execute() override;
    void undo() override;
    int id() const override { return 62; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_noteId;
    int m_oldPitch;
    int64_t m_oldStartTick;
    int m_newPitch;
    int64_t m_newStartTick;
};

class ResizeNoteCommand : public UndoCommand {
public:
    ResizeNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId,
                      int64_t oldDuration, int64_t newDuration);
    void execute() override;
    void undo() override;
    int id() const override { return 63; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_noteId;
    int64_t m_oldDuration;
    int64_t m_newDuration;
};

class SetNoteVelocityCommand : public UndoCommand {
public:
    SetNoteVelocityCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId,
                           int oldVelocity, int newVelocity);
    void execute() override;
    void undo() override;
    int id() const override { return 64; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    int64_t m_noteId;
    int m_oldVelocity;
    int m_newVelocity;
};

// --- Multi-note commands (used by the piano roll for multi-selection) ---

struct NoteMoveChange {
    int64_t noteId = -1;
    int oldPitch = 0;
    int64_t oldStartTick = 0;
    int newPitch = 0;
    int64_t newStartTick = 0;
};

class MoveNotesCommand : public UndoCommand {
public:
    MoveNotesCommand(Project& project, int trackIndex, int64_t eventId,
                     std::vector<NoteMoveChange> changes);
    void execute() override;
    void undo() override;
    int id() const override { return 65; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    void apply(bool useNew);
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    std::vector<NoteMoveChange> m_changes;
};

struct NoteResizeChange {
    int64_t noteId = -1;
    int64_t oldDuration = 0;
    int64_t newDuration = 0;
};

class ResizeNotesCommand : public UndoCommand {
public:
    ResizeNotesCommand(Project& project, int trackIndex, int64_t eventId,
                       std::vector<NoteResizeChange> changes);
    void execute() override;
    void undo() override;
    int id() const override { return 66; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    void apply(bool useNew);
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    std::vector<NoteResizeChange> m_changes;
};

struct NoteVelocityChange {
    int64_t noteId = -1;
    int oldVelocity = 0;
    int newVelocity = 0;
};

class SetNotesVelocityCommand : public UndoCommand {
public:
    SetNotesVelocityCommand(Project& project, int trackIndex, int64_t eventId,
                            std::vector<NoteVelocityChange> changes);
    void execute() override;
    void undo() override;
    int id() const override { return 67; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    void apply(bool useNew);
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    std::vector<NoteVelocityChange> m_changes;
};

class RemoveNotesCommand : public UndoCommand {
public:
    RemoveNotesCommand(Project& project, int trackIndex, int64_t eventId,
                       const std::vector<int64_t>& noteIds);
    void execute() override;
    void undo() override;
    int id() const override { return 68; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    QJsonArray m_savedNotes;
};

class DuplicateNotesCommand : public UndoCommand {
public:
    DuplicateNotesCommand(Project& project, int trackIndex, int64_t eventId,
                          const std::vector<int64_t>& sourceNoteIds);
    void execute() override;
    void undo() override;
    int id() const override { return 69; }
    bool requiresPluginWindowsClose() const override { return false; }
    const std::vector<int64_t>& createdNoteIds() const { return m_createdNoteIds; }
private:
    Project& m_project;
    int m_trackIndex;
    int64_t m_eventId;
    QJsonArray m_sourceNotes;
    std::vector<int64_t> m_createdNoteIds;
};
