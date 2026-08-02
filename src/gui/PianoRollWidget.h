#pragma once
#include <QWidget>
#include <cstdint>

class Project;
class MidiClip;
class UndoStack;

class PianoRollWidget : public QWidget {
    Q_OBJECT
public:
    PianoRollWidget(Project& project, UndoStack& undo, int trackIndex, int64_t eventId,
                    QWidget* parent = nullptr);

    bool reload();
    int trackIndex() const { return m_trackIndex; }
    int64_t eventId() const { return m_eventId; }
    int snapDiv() const { return m_snapDiv; }
    void setSnapDiv(int div);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    static constexpr int kKeysWidth = 56;
    static constexpr int kRowHeight = 8;
    static constexpr int kEdgeWidth = 6;

    struct NoteRect {
        int64_t noteId = -1;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool rightEdge = false;
    };

    MidiClip* clip() const;
    int64_t xToTick(int x) const;
    int tickToX(int64_t tick) const;
    int pitchToY(int pitch) const;
    int yToPitch(int y) const;
    int64_t snapTick(int64_t tick) const;
    NoteRect noteRectAt(int x, int y) const;

    Project& m_project;
    UndoStack& m_undo;
    int m_trackIndex;
    int64_t m_eventId;

    double m_pixelsPerTick = 0.06;
    int m_snapDiv = 4;
    int m_lastVelocity = 100;
    int m_selectedNoteId = -1;

    enum class DragMode { None, Move, Resize };
    DragMode m_dragMode = DragMode::None;
    int m_dragNoteId = -1;
    int m_dragOrigPitch = 0;
    int64_t m_dragOrigStartTick = 0;
    int m_dragOrigDuration = 0;
    int m_dragMouseX = 0;
    int m_dragMouseY = 0;
};
