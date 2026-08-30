#pragma once
#include <QWidget>
#include <QColor>
#include <QPoint>
#include <algorithm>
#include <cstdint>
#include "core/Constants.h"
#include "core/TimeUtils.h"

class QPainter;
class QMenu;

// Common base for the time-based rulers (TimelineRuler / MeasureRuler): owns
// scroll/zoom/snap state, the loop + record-region drag handles, the context
// menu, and the shared range-bar + playhead painting. Subclasses only paint
// their own tick/grid content via paintTicks().
class RangeRulerWidget : public QWidget {
    Q_OBJECT
public:
    RangeRulerWidget(int fixedHeight, QWidget* parent = nullptr);

    void setScrollOffset(int64_t offset) { m_scrollOffset = offset; update(); }
    int64_t scrollOffset() const { return m_scrollOffset; }

    void setZoom(double pixelsPerSample) {
        m_pixelsPerSample = std::clamp(pixelsPerSample, vvvdaw::MinZoom, vvvdaw::MaxZoom);
        update();
    }
    double zoom() const { return m_pixelsPerSample; }

    void setSampleRate(int rate) { m_sampleRate = rate; update(); }

    void setPlayheadPosition(int64_t sample) { m_playheadPos = sample; update(); }
    int64_t playheadPosition() const { return m_playheadPos; }

    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
    void setSnapUnit(double samples) { m_snapUnit = samples; }

    void setLoop(int64_t start, int64_t end) { m_loopStart = start; m_loopEnd = end; update(); }
    void clearLoop() { m_loopStart = -1; m_loopEnd = -1; update(); }
    int64_t loopStart() const { return m_loopStart; }
    int64_t loopEnd() const { return m_loopEnd; }

    void setRecordRegion(int64_t start, int64_t end) { m_rrStart = start; m_rrEnd = end; update(); }
    void clearRecordRegion() { m_rrStart = -1; m_rrEnd = -1; update(); }
    int64_t recordRegionStart() const { return m_rrStart; }
    int64_t recordRegionEnd() const { return m_rrEnd; }

    // Selection created by dragging with the right mouse button. The selected
    // range is updated live (m_selectStartX/m_selectEndX) and committed when the
    // user picks "Create Loop" / "Create Record Region" from the release menu.
    bool isSelecting() const { return m_selecting; }

signals:
    void playheadClicked(int64_t sample);
    void loopCreated(int64_t start, int64_t end);
    void loopRemoved();
    void loopChanged(int64_t start, int64_t end);
    void recordRegionCreated(int64_t start, int64_t end);
    void recordRegionRemoved();
    void recordRegionChanged(int64_t start, int64_t end);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    virtual QColor backgroundColor() const { return QColor("#2a2a2a"); }
    virtual void paintTicks(QPainter& painter, int64_t startSample, int64_t endSample) = 0;

    int64_t sampleAtX(int x) const {
        int64_t sample = m_scrollOffset + static_cast<int64_t>(x / m_pixelsPerSample);
        if (sample < 0) sample = 0;
        if (m_snapToGrid)
            sample = TimeUtils::snapSample(sample, m_snapUnit);
        return sample;
    }
    int sampleToX(int64_t sample) const {
        return static_cast<int>((sample - m_scrollOffset) * m_pixelsPerSample);
    }

    // Release the right-mouse-button drag. The default implementation builds a
    // menu ("Create Loop" / "Create Record Region") and execs it; tests override
    // it to avoid blocking on the modal menu.
    virtual void popupCreateMenu(const QPoint& globalPos, int64_t start, int64_t end);
    // Plain right-click (no drag) with existing ranges: offers "Remove ...".
    virtual void popupRemoveMenu(const QPoint& globalPos);

    // The two menu builders, split out so tests can trigger the connected
    // actions without exec()ing a modal menu. They are not virtual.
    QMenu* buildCreateMenu(int64_t start, int64_t end);
    QMenu* buildRemoveMenu();

    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = vvvdaw::DefaultZoom;
    int m_sampleRate = vvvdaw::DefaultSampleRate;
    int64_t m_playheadPos = -1;
    bool m_snapToGrid = true;
    double m_snapUnit = vvvdaw::DefaultSnapUnitSamples;

    int64_t m_loopStart = -1;
    int64_t m_loopEnd = -1;
    int64_t m_rrStart = -1;
    int64_t m_rrEnd = -1;

private:
    enum class DragHandle { None, LoopStart, LoopEnd, RRStart, RREnd };

    DragHandle handleAtPos(int x) const;
    void drawRange(QPainter& painter, int64_t rangeStart, int64_t rangeEnd,
                   const QColor& fill, const QColor& border, const QColor& handleColor,
                   const QString& label);
    void drawSelectionPreview(QPainter& painter);

    // Right-button drag-to-select helpers (creation / removal).
    void beginRightButtonSelect(int x);
    void handleRightButtonDrag(int x);
    void handleRightButtonRelease(const QPoint& globalPos);

    // Left-button range-handle drag helpers.
    void beginHandleDrag(DragHandle handle, int mouseX);
    void finishHandleDrag();

    void createLoopFromRange(int64_t start, int64_t end);
    void createRecordRegionFromRange(int64_t start, int64_t end);

    DragHandle m_dragHandle = DragHandle::None;
    int m_dragStartMouseX = 0;
    int64_t m_dragStartValue = 0;
    bool m_dragging = false;

    // Right-button drag-to-select state (live preview during the drag).
    bool m_rightPressed = false;
    bool m_selecting = false;
    int m_selectStartX = 0;
    int m_selectEndX = 0;
};
