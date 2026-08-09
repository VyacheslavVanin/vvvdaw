#include "BusLevelMeter.h"
#include "audio/AudioUtils.h"
#include <QPainter>

namespace {
constexpr int kClipHoldMs = 1200;
constexpr float kMaxDb = 0.0f;
constexpr float kMinDb = -60.0f;

// Decay applied to the displayed level on every update so the bar falls
// smoothly instead of snapping to zero.
constexpr float kPeakDecay = 0.85f;
} // namespace

BusLevelMeter::BusLevelMeter(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(10, 40);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void BusLevelMeter::setPeak(float linearPeak) {
    m_peak = std::max(linearPeak, m_peak * kPeakDecay);
    update();
}

void BusLevelMeter::setClipping(bool clipped) {
    if (clipped) {
        m_clipLatch = true;
        m_clipTimer.start();
    } else if (m_clipLatch && !m_clipTimer.isValid()) {
        m_clipTimer.start();
    }
    update();
}

void BusLevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    const int marginX = std::max(1, w / 6);
    const int barW = std::max(1, w - marginX * 2);
    const int clipH = std::max(4, w / 4);

    // Background column.
    p.fillRect(marginX, 0, barW, h, QColor("#1c1c1c"));

    float db = linearToDecibels(m_peak);
    float frac = (db - kMinDb) / (kMaxDb - kMinDb);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    int barH = static_cast<int>((h - clipH) * frac);
    if (barH > 0) {
        QLinearGradient grad(0, h - clipH, 0, clipH);
        grad.setColorAt(0.0, QColor("#3ec93e")); // green
        grad.setColorAt(0.68, QColor("#d8c428")); // yellow
        grad.setColorAt(0.86, QColor("#e08a2b")); // orange
        grad.setColorAt(1.0, QColor("#e23b3b")); // red
        QRect bar(marginX, h - clipH - barH, barW, barH);
        p.fillRect(bar, grad);
    }

    // Clip LED above the bar.
    QRect led(marginX, 0, barW, clipH - 2);
    bool clipOn = m_clipLatch &&
                  (!m_clipTimer.isValid() || m_clipTimer.elapsed() < kClipHoldMs);
    if (clipOn)
        p.fillRect(led, QColor("#ff3030"));
    else
        p.fillRect(led, QColor("#3a2020"));
}
