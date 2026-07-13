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

private:
    std::vector<std::unique_ptr<UndoCommand>> m_undoStack;
    std::vector<std::unique_ptr<UndoCommand>> m_redoStack;
};
