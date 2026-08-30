#pragma once
#include <QWidget>
#include <QPoint>
#include "commands/MidiCommands.h"
#include <cstdint>
#include <set>
#include <vector>

class Project;
class UndoStack;
class MidiClip;
class MidiEvent;

class VelocityEditorWidget : public QWidget {
    Q_OBJECT
public:
    VelocityEditorWidget(Project& project, UndoStack& undo,
                         int trackIndex, int64_t eventId, QWidget* parent = nullptr);

    bool reload();
    void setPixelsPerTick(double p);
    double pixelsPerTick() const { return m_pixelsPerTick; }
    void setSelection(const std::set<int64_t>& ids);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static constexpr int kKeysWidth = 56;
    static constexpr int kLaneHeight = 80;
    static constexpr int kBarWidth = 5;

    MidiClip* clip() const;
    MidiEvent* currentEvent() const;
    int tickToX(int64_t tick) const;
    int velocityFromY(int y) const;
    int64_t velocityBarAt(int x) const;
    std::vector<int64_t> notesInXRange(int x0, int x1) const;
    void beginVelocityDrag(const QPoint& pos);
    void updateVelocityDrag(const QPoint& pos);
    void endVelocityDrag();

    Project& m_project;
    UndoStack& m_undo;
    int m_trackIndex;
    int64_t m_eventId;

    double m_pixelsPerTick = 0.06;
    std::set<int64_t> m_selectedNoteIds;

    bool m_velDragging = false;
    bool m_velSelectionMode = false;
    int m_velDragValue = 1;
    int m_velLastX = 0;
    int64_t m_velActiveNote = -1;
    std::vector<NoteVelocityChange> m_velChanges;
};
