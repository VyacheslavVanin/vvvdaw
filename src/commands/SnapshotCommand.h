#pragma once
#include "core/UndoCommand.h"
#include <QJsonObject>

class Project;

class SnapshotCommand : public UndoCommand {
public:
    SnapshotCommand(Project& project);
    void execute() override;
    void undo() override;
    int id() const override { return -1; }
private:
    Project& m_project;
    QJsonObject m_before;
    QJsonObject m_after;
    bool m_redoing = false;
};
