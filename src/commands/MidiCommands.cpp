#include "MidiCommands.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/MidiEvent.h"
#include <QJsonArray>
#include <QJsonObject>

static QJsonObject midiEventToJson(const MidiEvent& event) {
    QJsonObject eObj;
    eObj["startSample"] = static_cast<qint64>(event.startSample());
    eObj["offsetSample"] = static_cast<qint64>(event.offsetSample());
    eObj["durationSample"] = static_cast<qint64>(event.durationSample());
    if (event.clip())
        eObj["clip"] = event.clip()->toJson();
    if (!event.takes().empty()) {
        QJsonArray takesArr;
        for (const auto& take : event.takes())
            takesArr.append(take->toJson());
        eObj["takes"] = takesArr;
        eObj["activeTakeIndex"] = event.activeTakeIndex();
    }
    return eObj;
}

static MidiEvent midiEventFromJson(const QJsonObject& eObj) {
    MidiEvent event;
    if (eObj.contains("clip")) {
        auto clip = std::make_shared<MidiClip>();
        clip->fromJson(eObj["clip"].toObject());
        event.setClip(clip);
    }
    event.setStartSample(static_cast<int64_t>(eObj["startSample"].toVariant().toLongLong()));
    event.setOffsetSample(static_cast<int64_t>(eObj["offsetSample"].toVariant().toLongLong()));
    event.setDurationSample(static_cast<int64_t>(eObj["durationSample"].toVariant().toLongLong()));
    if (eObj.contains("takes")) {
        const QJsonArray takesArr = eObj["takes"].toArray();
        for (const auto& takeVal : takesArr) {
            auto takeClip = std::make_shared<MidiClip>();
            takeClip->fromJson(takeVal.toObject());
            event.takes().push_back(takeClip);
        }
        event.setActiveTakeIndex(eObj["activeTakeIndex"].toInt(-1));
        if (event.activeTakeIndex() >= 0 && event.activeTakeIndex() < static_cast<int>(event.takes().size()))
            event.setClip(event.takes()[event.activeTakeIndex()]);
    }
    return event;
}

static MidiEvent* findMidiEvent(Project& project, int trackIndex, int64_t eventId) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(project.tracks().size()))
        return nullptr;
    return project.tracks()[trackIndex].findMidiEvent(eventId);
}

static MidiClip* activeClipForEvent(Project& project, int trackIndex, int64_t eventId) {
    MidiEvent* event = findMidiEvent(project, trackIndex, eventId);
    if (!event) return nullptr;
    return event->activeClip().get();
}

static QJsonObject noteToJson(const MidiNote& note) {
    QJsonObject obj;
    obj["id"] = static_cast<qint64>(note.id);
    obj["pitch"] = note.pitch;
    obj["velocity"] = note.velocity;
    obj["startTick"] = static_cast<qint64>(note.startTick);
    obj["durationTicks"] = static_cast<qint64>(note.durationTicks);
    return obj;
}

static MidiNote noteFromJson(const QJsonObject& obj) {
    MidiNote note;
    note.id = obj["id"].toVariant().toLongLong();
    note.pitch = obj["pitch"].toInt(60);
    note.velocity = obj["velocity"].toInt(100);
    note.startTick = static_cast<int64_t>(obj["startTick"].toVariant().toLongLong());
    note.durationTicks = static_cast<int64_t>(obj["durationTicks"].toVariant().toLongLong());
    return note;
}

// --- AddMidiEventCommand ---

AddMidiEventCommand::AddMidiEventCommand(Project& project, int trackIndex, QJsonObject eventJson)
    : m_project(project), m_trackIndex(trackIndex), m_eventJson(eventJson) {}

void AddMidiEventCommand::execute() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    MidiEvent event = midiEventFromJson(m_eventJson);
    m_project.tracks()[m_trackIndex].addMidiEvent(event);
    m_createdEventId = m_project.tracks()[m_trackIndex].midiEvents().back().id();
}

void AddMidiEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    auto& events = m_project.tracks()[m_trackIndex].midiEvents();
    if (!events.empty())
        m_project.tracks()[m_trackIndex].removeMidiEvent(events.back().id());
}

// --- RemoveMidiEventCommand ---

RemoveMidiEventCommand::RemoveMidiEventCommand(Project& project, int trackIndex, int64_t eventId)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_project.tracks().size())) {
        auto* ev = m_project.tracks()[trackIndex].findMidiEvent(eventId);
        if (ev) m_savedEvent = midiEventToJson(*ev);
    }
}

void RemoveMidiEventCommand::execute() {
    if (m_trackIndex >= 0 && m_trackIndex < static_cast<int>(m_project.tracks().size()))
        m_project.tracks()[m_trackIndex].removeMidiEvent(m_eventId);
}

void RemoveMidiEventCommand::undo() {
    if (m_trackIndex < 0 || m_trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    MidiEvent event = midiEventFromJson(m_savedEvent);
    m_project.tracks()[m_trackIndex].importMidiEvent(event);
}

// --- MoveMidiEventCommand ---

MoveMidiEventCommand::MoveMidiEventCommand(Project& project, int trackIndex, int64_t eventId,
                                           int64_t oldStart, int64_t newStart)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldStart(oldStart), m_newStart(newStart) {}

void MoveMidiEventCommand::execute() {
    MidiEvent* ev = findMidiEvent(m_project, m_trackIndex, m_eventId);
    if (ev) ev->setStartSample(m_newStart);
}

void MoveMidiEventCommand::undo() {
    MidiEvent* ev = findMidiEvent(m_project, m_trackIndex, m_eventId);
    if (ev) ev->setStartSample(m_oldStart);
}

bool MoveMidiEventCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const MoveMidiEventCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId) return false;
    m_newStart = cmd->m_newStart;
    return true;
}

// --- TrimMidiEventCommand ---

TrimMidiEventCommand::TrimMidiEventCommand(Project& project, int trackIndex, int64_t eventId,
                                           int64_t oldOffset, int64_t oldDuration,
                                           int64_t newOffset, int64_t newDuration)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_oldOffset(oldOffset), m_oldDuration(oldDuration),
      m_newOffset(newOffset), m_newDuration(newDuration) {}

void TrimMidiEventCommand::execute() {
    MidiEvent* ev = findMidiEvent(m_project, m_trackIndex, m_eventId);
    if (ev) {
        ev->setOffsetSample(m_newOffset);
        ev->setDurationSample(m_newDuration);
    }
}

void TrimMidiEventCommand::undo() {
    MidiEvent* ev = findMidiEvent(m_project, m_trackIndex, m_eventId);
    if (ev) {
        ev->setOffsetSample(m_oldOffset);
        ev->setDurationSample(m_oldDuration);
    }
}

bool TrimMidiEventCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const TrimMidiEventCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId) return false;
    m_newOffset = cmd->m_newOffset;
    m_newDuration = cmd->m_newDuration;
    return true;
}

// --- AddNoteCommand ---

AddNoteCommand::AddNoteCommand(Project& project, int trackIndex, int64_t eventId,
                               int pitch, int velocity, int64_t startTick, int64_t durationTicks)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_pitch(pitch), m_velocity(velocity),
      m_startTick(startTick), m_durationTicks(durationTicks) {}

void AddNoteCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    m_createdNoteId = clip->addNote(m_pitch, m_velocity, m_startTick, m_durationTicks);
}

void AddNoteCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip && m_createdNoteId >= 0)
        clip->removeNote(m_createdNoteId);
}

// --- RemoveNoteCommand ---

RemoveNoteCommand::RemoveNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId), m_noteId(noteId) {
    MidiClip* clip = activeClipForEvent(project, trackIndex, eventId);
    if (clip) {
        MidiNote* note = clip->findNote(noteId);
        if (note)
            m_savedNote = noteToJson(*note);
    }
}

void RemoveNoteCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip)
        clip->removeNote(m_noteId);
}

void RemoveNoteCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    MidiNote note = noteFromJson(m_savedNote);
    clip->importNote(note);
}

// --- MoveNoteCommand ---

MoveNoteCommand::MoveNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId,
                                 int oldPitch, int64_t oldStartTick,
                                 int newPitch, int64_t newStartTick)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId), m_noteId(noteId),
      m_oldPitch(oldPitch), m_oldStartTick(oldStartTick),
      m_newPitch(newPitch), m_newStartTick(newStartTick) {}

void MoveNoteCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->pitch = m_newPitch;
            note->startTick = m_newStartTick;
            clip->bumpRevision();
        }
    }
}

void MoveNoteCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->pitch = m_oldPitch;
            note->startTick = m_oldStartTick;
            clip->bumpRevision();
        }
    }
}

bool MoveNoteCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const MoveNoteCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId
        || m_noteId != cmd->m_noteId)
        return false;
    m_newPitch = cmd->m_newPitch;
    m_newStartTick = cmd->m_newStartTick;
    return true;
}

// --- ResizeNoteCommand ---

ResizeNoteCommand::ResizeNoteCommand(Project& project, int trackIndex, int64_t eventId, int64_t noteId,
                                     int64_t oldDuration, int64_t newDuration)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId), m_noteId(noteId),
      m_oldDuration(oldDuration), m_newDuration(newDuration) {}

void ResizeNoteCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->durationTicks = m_newDuration;
            clip->bumpRevision();
        }
    }
}

void ResizeNoteCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->durationTicks = m_oldDuration;
            clip->bumpRevision();
        }
    }
}

bool ResizeNoteCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const ResizeNoteCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId
        || m_noteId != cmd->m_noteId)
        return false;
    m_newDuration = cmd->m_newDuration;
    return true;
}

// --- SetNoteVelocityCommand ---

SetNoteVelocityCommand::SetNoteVelocityCommand(Project& project, int trackIndex, int64_t eventId,
                                               int64_t noteId, int oldVelocity, int newVelocity)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId), m_noteId(noteId),
      m_oldVelocity(oldVelocity), m_newVelocity(newVelocity) {}

void SetNoteVelocityCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->velocity = m_newVelocity;
            clip->bumpRevision();
        }
    }
}

void SetNoteVelocityCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (clip) {
        MidiNote* note = clip->findNote(m_noteId);
        if (note) {
            note->velocity = m_oldVelocity;
            clip->bumpRevision();
        }
    }
}

bool SetNoteVelocityCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetNoteVelocityCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId
        || m_noteId != cmd->m_noteId)
        return false;
    m_newVelocity = cmd->m_newVelocity;
    return true;
}

// --- MoveNotesCommand ---

MoveNotesCommand::MoveNotesCommand(Project& project, int trackIndex, int64_t eventId,
                                   std::vector<NoteMoveChange> changes)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_changes(std::move(changes)) {}

void MoveNotesCommand::apply(bool useNew) {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (auto& ch : m_changes) {
        MidiNote* note = clip->findNote(ch.noteId);
        if (!note) continue;
        note->pitch = useNew ? ch.newPitch : ch.oldPitch;
        note->startTick = useNew ? ch.newStartTick : ch.oldStartTick;
    }
    clip->bumpRevision();
}

void MoveNotesCommand::execute() { apply(true); }
void MoveNotesCommand::undo() { apply(false); }

bool MoveNotesCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const MoveNotesCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId)
        return false;
    for (auto& ch : cmd->m_changes) {
        for (auto& mine : m_changes) {
            if (mine.noteId == ch.noteId) {
                mine.newPitch = ch.newPitch;
                mine.newStartTick = ch.newStartTick;
                break;
            }
        }
    }
    return true;
}

// --- ResizeNotesCommand ---

ResizeNotesCommand::ResizeNotesCommand(Project& project, int trackIndex, int64_t eventId,
                                       std::vector<NoteResizeChange> changes)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_changes(std::move(changes)) {}

void ResizeNotesCommand::apply(bool useNew) {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (auto& ch : m_changes) {
        MidiNote* note = clip->findNote(ch.noteId);
        if (!note) continue;
        note->durationTicks = useNew ? ch.newDuration : ch.oldDuration;
    }
    clip->bumpRevision();
}

void ResizeNotesCommand::execute() { apply(true); }
void ResizeNotesCommand::undo() { apply(false); }

bool ResizeNotesCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const ResizeNotesCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId)
        return false;
    for (auto& ch : cmd->m_changes) {
        for (auto& mine : m_changes) {
            if (mine.noteId == ch.noteId) {
                mine.newDuration = ch.newDuration;
                break;
            }
        }
    }
    return true;
}

// --- SetNotesVelocityCommand ---

SetNotesVelocityCommand::SetNotesVelocityCommand(Project& project, int trackIndex, int64_t eventId,
                                                 std::vector<NoteVelocityChange> changes)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId),
      m_changes(std::move(changes)) {}

void SetNotesVelocityCommand::apply(bool useNew) {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (auto& ch : m_changes) {
        MidiNote* note = clip->findNote(ch.noteId);
        if (!note) continue;
        note->velocity = useNew ? ch.newVelocity : ch.oldVelocity;
    }
    clip->bumpRevision();
}

void SetNotesVelocityCommand::execute() { apply(true); }
void SetNotesVelocityCommand::undo() { apply(false); }

bool SetNotesVelocityCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetNotesVelocityCommand*>(other);
    if (m_trackIndex != cmd->m_trackIndex || m_eventId != cmd->m_eventId)
        return false;
    for (auto& ch : cmd->m_changes) {
        for (auto& mine : m_changes) {
            if (mine.noteId == ch.noteId) {
                mine.newVelocity = ch.newVelocity;
                break;
            }
        }
    }
    return true;
}

// --- RemoveNotesCommand ---

RemoveNotesCommand::RemoveNotesCommand(Project& project, int trackIndex, int64_t eventId,
                                       const std::vector<int64_t>& noteIds)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId) {
    MidiClip* clip = activeClipForEvent(project, trackIndex, eventId);
    if (!clip) return;
    for (int64_t id : noteIds) {
        MidiNote* note = clip->findNote(id);
        if (note)
            m_savedNotes.append(noteToJson(*note));
    }
}

void RemoveNotesCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (const auto& v : m_savedNotes) {
        MidiNote note = noteFromJson(v.toObject());
        clip->removeNote(note.id);
    }
}

void RemoveNotesCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (const auto& v : m_savedNotes) {
        MidiNote note = noteFromJson(v.toObject());
        clip->importNote(note);
    }
}

// --- DuplicateNotesCommand ---

DuplicateNotesCommand::DuplicateNotesCommand(Project& project, int trackIndex, int64_t eventId,
                                             const std::vector<int64_t>& sourceNoteIds)
    : m_project(project), m_trackIndex(trackIndex), m_eventId(eventId) {
    MidiClip* clip = activeClipForEvent(project, trackIndex, eventId);
    if (!clip) return;
    for (int64_t id : sourceNoteIds) {
        MidiNote* note = clip->findNote(id);
        if (note)
            m_sourceNotes.append(noteToJson(*note));
    }
}

void DuplicateNotesCommand::execute() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    m_createdNoteIds.clear();
    for (const auto& v : m_sourceNotes) {
        MidiNote note = noteFromJson(v.toObject());
        int64_t newId = clip->addNote(note.pitch, note.velocity, note.startTick, note.durationTicks);
        m_createdNoteIds.push_back(newId);
    }
}

void DuplicateNotesCommand::undo() {
    MidiClip* clip = activeClipForEvent(m_project, m_trackIndex, m_eventId);
    if (!clip) return;
    for (int64_t id : m_createdNoteIds)
        clip->removeNote(id);
    m_createdNoteIds.clear();
}
