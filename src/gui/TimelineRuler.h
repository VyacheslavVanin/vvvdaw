#pragma once
#include <QWidget>
#include <cstdint>

class TimelineRuler : public QWidget {
    Q_OBJECT
public:
    explicit TimelineRuler(QWidget* parent = nullptr);

    void setScrollOffset(int64_t offset) { m_scrollOffset = offset; update(); }
    int64_t scrollOffset() const { return m_scrollOffset; }

    void setZoom(double pixelsPerSample) { m_pixelsPerSample = pixelsPerSample; update(); }
    double zoom() const { return m_pixelsPerSample; }

    void setPlayheadPosition(int64_t sample) { m_playheadPos = sample; update(); }
    int64_t playheadPosition() const { return m_playheadPos; }

    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }

    void setLoop(int64_t start, int64_t end) { m_loopStart = start; m_loopEnd = end; update(); }
    void clearLoop() { m_loopStart = -1; m_loopEnd = -1; update(); }
    int64_t loopStart() const { return m_loopStart; }
    int64_t loopEnd() const { return m_loopEnd; }

    void setRecordRegion(int64_t start, int64_t end) { m_rrStart = start; m_rrEnd = end; update(); }
    void clearRecordRegion() { m_rrStart = -1; m_rrEnd = -1; update(); }
    int64_t recordRegionStart() const { return m_rrStart; }
    int64_t recordRegionEnd() const { return m_rrEnd; }

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
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum class DragHandle { None, LoopStart, LoopEnd, RRStart, RREnd };

    int64_t sampleAtX(int x) const;
    DragHandle handleAtPos(int x) const;
    void drawRange(QPainter& painter, int64_t rangeStart, int64_t rangeEnd,
                   const QColor& fill, const QColor& border, const QColor& handleColor,
                   const QString& label);

    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = 0.001;
    int64_t m_playheadPos = -1;
    bool m_snapToGrid = true;

    int64_t m_loopStart = -1;
    int64_t m_loopEnd = -1;
    int64_t m_rrStart = -1;
    int64_t m_rrEnd = -1;

    // Drag state
    DragHandle m_dragHandle = DragHandle::None;
    int m_dragStartMouseX = 0;
    int64_t m_dragStartValue = 0;
    bool m_dragging = false;
};
