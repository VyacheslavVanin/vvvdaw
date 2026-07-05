#pragma once
#include <QWidget>

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

signals:
    void playheadClicked(int64_t sample);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = 0.001;
    int64_t m_playheadPos = -1;
    bool m_snapToGrid = true;
};
