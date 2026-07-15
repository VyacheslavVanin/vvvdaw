#pragma once

class UndoCommand {
public:
    virtual ~UndoCommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual int id() const { return -1; }
    virtual bool mergeWith(const UndoCommand* /*other*/) { return false; }
    virtual bool requiresPluginWindowsClose() const { return true; }
};
