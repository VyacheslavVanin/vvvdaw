#include "UndoStack.h"

bool UndoStack::mergeIntoLast(UndoCommand* cmd) {
    if (m_undoStack.empty())
        return false;
    UndoCommand* last = m_undoStack.back().get();
    return last->id() != -1 && last->id() == cmd->id() && last->mergeWith(cmd);
}

void UndoStack::append(std::unique_ptr<UndoCommand> cmd) {
    if (m_undoStack.size() >= MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
}

void UndoStack::execute(std::unique_ptr<UndoCommand> cmd) {
    if (mergeIntoLast(cmd.get())) {
        m_undoStack.back()->execute();
        return;
    }
    append(std::move(cmd));
    m_undoStack.back()->execute();
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
    if (mergeIntoLast(cmd.get()))
        return;
    append(std::move(cmd));
}

void UndoStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
