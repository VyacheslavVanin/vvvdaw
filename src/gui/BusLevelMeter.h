#pragma once
#include <QWidget>
#include <QElapsedTimer>

// Vertical output level meter with a dB scale and a separate clipping LED.
// Feed it the post-fader linear peak and clip state of a bus; it renders the
// level as a green/yellow/red bar on a dB tick scale and holds the clip LED
// lit for a short window after the last clip so it stays visible. setVolume()
// draws a marker on the scale at the bus's (linear) volume so the fader and
// the meter share one dB scale.
class BusLevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit BusLevelMeter(QWidget* parent = nullptr);

    void setPeak(float linearPeak);
    void setClipping(bool clipped);
    // Marks the current volume position on the dB scale (linear volume).
    void setVolume(float linearVolume);
    void clearPeak() { m_peak = 0.0f; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // Y pixel of a dB value on the scale (top = 0 dB, bottom = -60 dB).
    int dbToY(float db) const;

    float m_peak = 0.0f;
    float m_volumeDb = 0.0f;
    bool m_hasVolume = false;
    bool m_clipLatch = false;
    QElapsedTimer m_clipTimer;
};
