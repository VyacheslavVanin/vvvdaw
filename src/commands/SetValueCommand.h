#pragma once
#include "core/UndoCommand.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"

namespace vvvcmd {

// Resolve the object of type Obj addressed by `index` inside the project,
// or nullptr when the index is out of range.
template <class Obj>
struct ObjectAccess;

template <>
struct ObjectAccess<Track> {
    static Track* get(Project& project, int index) { return project.trackAt(index); }
};

template <>
struct ObjectAccess<AudioBus> {
    static AudioBus* get(Project& project, int index) { return project.busAt(index); }
};

template <>
struct ObjectAccess<Instrument> {
    static Instrument* get(Project& project, int index) { return project.instrumentAt(index); }
};

template <class Obj>
Obj* projectObject(Project& project, int index) {
    return ObjectAccess<Obj>::get(project, index);
}

// Undoable "set one scalar/string property on a track/bus/instrument".
// Set is a pointer to the mutating member function (e.g. &Track::setVolume);
// oldValue/newValue are captured at construction. CommandId, Mergeable and
// CloseWindows mirror the per-command behavior of the previous hand-written
// classes.
template <class Obj, class Value, int CommandId, bool Mergeable, bool CloseWindows, auto Set>
class SetValueCommand : public UndoCommand {
public:
    SetValueCommand(Project& project, int index, Value oldValue, Value newValue)
        : m_project(project), m_index(index), m_oldValue(oldValue), m_newValue(newValue) {}

    void execute() override {
        if (Obj* obj = projectObject<Obj>(m_project, m_index))
            (obj->*Set)(m_newValue);
    }

    void undo() override {
        if (Obj* obj = projectObject<Obj>(m_project, m_index))
            (obj->*Set)(m_oldValue);
    }

    int id() const override { return CommandId; }

    bool mergeWith(const UndoCommand* other) override {
        if constexpr (!Mergeable)
            return false;
        auto* cmd = static_cast<const SetValueCommand*>(other);
        if (m_index != cmd->m_index)
            return false;
        m_newValue = cmd->m_newValue;
        return true;
    }

    bool requiresPluginWindowsClose() const override { return CloseWindows; }

private:
    Project& m_project;
    int m_index;
    Value m_oldValue;
    Value m_newValue;
};

} // namespace vvvcmd
