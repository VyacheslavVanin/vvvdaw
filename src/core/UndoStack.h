#pragma once
#include "UndoCommand.h"
#include <memory>
#include <vector>

class UndoStack {
public:
    static constexpr size_t MAX_UNDO = 100;

    void execute(std::unique_ptr<UndoCommand> cmd);
    void push(std::unique_ptr<UndoCommand> cmd);
    bool undo();
    bool redo();
    void clear();

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    UndoCommand* topCommand() const { return m_undoStack.empty() ? nullptr : m_undoStack.back().get(); }

private:
    // If the incoming command can be merged into the top of the undo stack,
    // merges it and returns true.
    bool mergeIntoLast(UndoCommand* cmd);
    // Appends a command, trimming the stack to MAX_UNDO and clearing redo.
    void append(std::unique_ptr<UndoCommand> cmd);

    std::vector<std::unique_ptr<UndoCommand>> m_undoStack;
    std::vector<std::unique_ptr<UndoCommand>> m_redoStack;
};
