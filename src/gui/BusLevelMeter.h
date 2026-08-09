#pragma once
#include <QWidget>
#include <QElapsedTimer>

// Vertical output level meter with a dB scale and a separate clipping LED.
// Feed it the post-fader linear peak and clip state of a bus; it renders the
// level as a green/yellow/red bar and holds the clip LED lit for a short
// window after the last clip so it stays visible.
class BusLevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit BusLevelMeter(QWidget* parent = nullptr);

    void setPeak(float linearPeak);
    void setClipping(bool clipped);
    void clearPeak() { m_peak = 0.0f; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_peak = 0.0f;
    bool m_clipLatch = false;
    QElapsedTimer m_clipTimer;
};
