#include "UndoStack.h"

void UndoStack::push(const QJsonObject& state) {
    if (m_undoStack.size() >= MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());
    m_undoStack.push_back(state);
    m_redoStack.clear();
}

std::optional<QJsonObject> UndoStack::undo(const QJsonObject& currentState) {
    if (m_undoStack.empty())
        return std::nullopt;

    m_redoStack.push_back(currentState);
    auto state = m_undoStack.back();
    m_undoStack.pop_back();
    return state;
}

std::optional<QJsonObject> UndoStack::redo(const QJsonObject& currentState) {
    if (m_redoStack.empty())
        return std::nullopt;

    m_undoStack.push_back(currentState);
    auto state = m_redoStack.back();
    m_redoStack.pop_back();
    return state;
}

void UndoStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
