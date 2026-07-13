#include "ProjectCommands.h"
#include "model/Project.h"

// --- SetTempoCommand ---

SetTempoCommand::SetTempoCommand(Project& project, double oldValue, double newValue)
    : m_project(project), m_oldValue(oldValue), m_newValue(newValue) {}

void SetTempoCommand::execute() {
    m_project.setTempo(m_newValue);
}

void SetTempoCommand::undo() {
    m_project.setTempo(m_oldValue);
}

bool SetTempoCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = static_cast<const SetTempoCommand*>(other);
    m_newValue = cmd->m_newValue;
    return true;
}

// --- SetTimeSigCommand ---

SetTimeSigCommand::SetTimeSigCommand(Project& project, int oldNum, int oldDen, int newNum, int newDen)
    : m_project(project), m_oldNum(oldNum), m_oldDen(oldDen), m_newNum(newNum), m_newDen(newDen) {}

void SetTimeSigCommand::execute() {
    m_project.setTimeSignature(m_newNum, m_newDen);
}

void SetTimeSigCommand::undo() {
    m_project.setTimeSignature(m_oldNum, m_oldDen);
}
