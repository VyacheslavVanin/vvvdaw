#pragma once
#include <QJsonObject>
#include <optional>
#include <vector>

class UndoStack {
public:
    static constexpr size_t MAX_UNDO = 100;

    void push(const QJsonObject& state);
    std::optional<QJsonObject> undo(const QJsonObject& currentState);
    std::optional<QJsonObject> redo(const QJsonObject& currentState);
    void clear();

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

private:
    std::vector<QJsonObject> m_undoStack;
    std::vector<QJsonObject> m_redoStack;
};
