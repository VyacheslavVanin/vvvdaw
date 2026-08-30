#pragma once
#include <QWidget>
#include "core/Constants.h"

class TrackPanelWidget;
class QSplitter;
class QVBoxLayout;

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

    int rowHeight() const { return m_rowHeight; }
    int minimumRowHeight() const;
    void applyHeight(int h);

signals:
    void resizeStarted(int trackIndex, int startHeight);
    void resizeDragged(int trackIndex, int newHeight, bool allTracks);
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
    QSplitter* m_splitter = nullptr;
    QWidget* m_content = nullptr;
    QWidget* m_handle = nullptr;
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
