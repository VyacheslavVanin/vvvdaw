#pragma once
#include <QWidget>
#include <QPoint>
#include <cstdint>
#include <set>
#include <vector>

class Project;
class MidiClip;
class MidiEvent;
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
    int selectedCount() const { return static_cast<int>(m_selectedNoteIds.size()); }

    void setPlayheadSample(int64_t sample) {
        if (m_playheadSample != sample) {
            m_playheadSample = sample;
            update();
        }
    }

signals:
    void playheadSetRequested(int64_t sample);

public:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
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

    struct NoteOrig {
        int64_t noteId = -1;
        int pitch = 0;
        int64_t startTick = 0;
        int64_t durationTicks = 0;
    };

    MidiClip* clip() const;
    MidiEvent* currentEvent() const;
    int64_t clickToTimelineSample(int x) const;
    int64_t xToTick(int x) const;
    int tickToX(int64_t tick) const;
    int pitchToY(int pitch) const;
    int yToPitch(int y) const;
    int64_t snapTick(int64_t tick) const;
    int64_t snapTickFloor(int64_t tick) const;
    NoteRect noteRectAt(int x, int y) const;
    void collectNoteRects(std::vector<NoteRect>& out) const;
    void selectNotesInRect(const QRect& rect, bool add);
    void clearDragState();
    void beginNoteDrag(int noteId, const QPoint& pos, bool resize);
    void duplicateSelection();
    void addNoteAt(const QPoint& pos);

    Project& m_project;
    UndoStack& m_undo;
    int m_trackIndex;
    int64_t m_eventId;

    double m_pixelsPerTick = 0.06;
    int m_snapDiv = 4;
    int m_lastVelocity = 100;
    int64_t m_playheadSample = -1;

    std::set<int64_t> m_selectedNoteIds;

    enum class DragMode { None, Move, Resize };
    DragMode m_dragMode = DragMode::None;
    std::vector<NoteOrig> m_dragOriginals;
    int m_dragMouseX = 0;
    int m_dragMouseY = 0;

    bool m_rubberBanding = false;
    QPoint m_rubberStart;
    QPoint m_rubberCurrent;
};
