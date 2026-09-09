#pragma once
#include <QWidget>
#include <QColor>
#include <vector>
#include "core/Constants.h"

class TrackPanelWidget;
class TrackColorBar;
class QSplitter;
class QVBoxLayout;

// The bottom resize handle of a track row. It is painted in the colors of the
// row above it — the panel column and the timeline column — so it blends with
// the row instead of reading as a dark stripe between tracks. Hover lightens
// it slightly; the SizeVer cursor signals the resize affordance.
class TrackResizeHandle : public QWidget {
public:
    explicit TrackResizeHandle(QWidget* parent = nullptr);

    void setSegmentColors(const QColor& left, const QColor& right, int splitX);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QColor m_left;
    QColor m_right;
    int m_splitX = 0;
    bool m_hover = false;
};

// A single track row container: the left panel + the plugin/view splitter,
// plus a bottom resize handle. It owns the mouse gestures for resizing
// (drag the bottom edge, Shift = all rows) and for reordering tracks
// (drag the panel background; MainWindow shows the insertion preview).
class TrackRowWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackRowWidget(QWidget* parent = nullptr);

    void setTrackIndex(int index) { m_trackIndex = index; }
    int trackIndex() const { return m_trackIndex; }

    // Place the panel and the plugin/view splitter into the row and add the
    // bottom resize handle. Both widgets become children of this row.
    void assemble(TrackPanelWidget* panel, QSplitter* splitter);

    TrackPanelWidget* panel() const { return m_panel; }
    TrackColorBar* colorBar() const { return m_colorBar; }
    // Optional thin vertical color strip placed to the left of the panel.
    // Set it before assemble(); it is inserted as the leftmost cell.
    void setColorBar(TrackColorBar* bar) { m_colorBar = bar; }

    // Paint the bottom resize handle in the row's colors (panel column /
    // timeline column) so it blends with the row above it instead of reading
    // as a dark stripe. `tint` is the track's effective display color.
    void setHandleColors(const QColor& tint, bool alternateRow);

    int rowHeight() const { return m_rowHeight; }
    int minimumRowHeight() const;
    void applyHeight(int h);

signals:
    void resizeStarted(int trackIndex, int startHeight, QPoint globalPressPos);
    void resizeDragged(int trackIndex, int newHeight, QPoint globalPos, bool allTracks);
    void resizeFinished(int trackIndex, int oldHeight, int newHeight, bool allTracks);
    void reorderDragStarted(int trackIndex);
    void reorderDragMoved(int trackIndex, QPoint globalPos);
    void reorderDragFinished(int trackIndex, QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int clampHeight(int h) const;

    TrackPanelWidget* m_panel = nullptr;
    TrackColorBar* m_colorBar = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_content = nullptr;
    TrackResizeHandle* m_handle = nullptr;
    int m_trackIndex = -1;
    int m_rowHeight = vvvdaw::DefaultTrackHeight;

    bool m_resizeDragging = false;
    int m_resizeStartGlobalY = 0;
    int m_resizeStartHeight = 0;
    bool m_resizeAll = false;

    bool m_reorderCandidate = false;
    bool m_reorderDragging = false;
    QPoint m_reorderStartGlobal;
};

// Scale every start height proportionally so the bottom edge of the row whose
// heights sum to `pressBottom` lands at `targetBottom` — the Shift-drag
// "resize all tracks" geometry that keeps the grabbed handle under the cursor.
// Returns `startHeights` unchanged when `pressBottom` is not positive; each
// result is clamped to [TrackResizeHandleHeight + 1, MaxTrackHeight] (per-row
// minimums are enforced by the callers).
std::vector<int> scaleTrackHeights(const std::vector<int>& startHeights,
                                   int pressBottom, int targetBottom);
