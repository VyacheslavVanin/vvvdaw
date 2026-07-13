#include "UndoStack.h"

void UndoStack::execute(std::unique_ptr<UndoCommand> cmd) {
    if (!m_undoStack.empty()) {
        auto* last = m_undoStack.back().get();
        if (last->id() != -1 && last->id() == cmd->id() && last->mergeWith(cmd.get())) {
            last->execute();
            return;
        }
    }

    if (m_undoStack.size() >= MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());
    m_undoStack.push_back(std::move(cmd));
    m_undoStack.back()->execute();
    m_redoStack.clear();
}

bool UndoStack::undo() {
    if (m_undoStack.empty())
        return false;

    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd->undo();
    m_redoStack.push_back(std::move(cmd));
    return true;
}

bool UndoStack::redo() {
    if (m_redoStack.empty())
        return false;

    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd->execute();
    m_undoStack.push_back(std::move(cmd));
    return true;
}

void UndoStack::push(std::unique_ptr<UndoCommand> cmd) {
    if (!m_undoStack.empty()) {
        auto* last = m_undoStack.back().get();
        if (last->id() != -1 && last->id() == cmd->id() && last->mergeWith(cmd.get())) {
            return;
        }
    }

    if (m_undoStack.size() >= MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
}

void UndoStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
