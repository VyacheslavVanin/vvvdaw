#pragma once
#include "core/UndoCommand.h"

class Project;

class SetTempoCommand : public UndoCommand {
public:
    SetTempoCommand(Project& project, double oldValue, double newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 30; }
    bool mergeWith(const UndoCommand* other) override;
private:
    Project& m_project;
    double m_oldValue;
    double m_newValue;
    // Tempo value corresponding to the timeline's current state. Used to apply
    // the incremental rescale when commands are merged (the timeline has already
    // been rescaled for previously merged values, so rescaling from m_oldValue
    // would over-compound).
    double m_appliedTempo;
};

class SetTimeSigCommand : public UndoCommand {
public:
    SetTimeSigCommand(Project& project, int oldNum, int oldDen, int newNum, int newDen);
    void execute() override;
    void undo() override;
    int id() const override { return 31; }
private:
    Project& m_project;
    int m_oldNum, m_oldDen;
    int m_newNum, m_newDen;
};
