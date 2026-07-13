#include "SnapshotCommand.h"
#include "model/Project.h"

SnapshotCommand::SnapshotCommand(Project& project)
    : m_project(project), m_before(project.toJson()) {}

void SnapshotCommand::execute() {
    if (m_redoing && !m_after.isEmpty())
        m_project.fromJson(m_after);
}

void SnapshotCommand::undo() {
    if (m_after.isEmpty())
        m_after = m_project.toJson();
    m_redoing = true;
    m_project.fromJson(m_before);
}
