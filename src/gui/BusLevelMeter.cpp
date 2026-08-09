#include "BusLevelMeter.h"
#include "audio/AudioUtils.h"
#include <QPainter>

namespace {
constexpr int kClipHoldMs = 1200;
constexpr float kMaxDb = 0.0f;
constexpr float kMinDb = -60.0f;

// Major dB points on the scale, labelled with their value.
const float kMajorTicks[] = { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
// Minor dB points between the majors, drawn as short ticks without labels.
const float kMinorTicks[] = { -6.0f, -18.0f, -30.0f, -42.0f, -54.0f };

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

void BusLevelMeter::setVolume(float linearVolume) {
    m_volumeDb = linearToDecibels(linearVolume);
    m_hasVolume = true;
    update();
}

int BusLevelMeter::dbToY(float db) const {
    if (db < kMinDb) db = kMinDb;
    if (db > kMaxDb) db = kMaxDb;
    const int h = height();
    const int clipH = std::max(4, width() / 4);
    const int usable = h - clipH;
    const float frac = (db - kMinDb) / (kMaxDb - kMinDb);
    return clipH + usable - static_cast<int>(usable * frac);
}

void BusLevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();
    const int clipH = std::max(4, w / 4);
    const int barW = 8;
    const int tickW = 3;
    // Left of the bar: dB labels, then tick marks, then the bar column.
    const int labelW = std::max(8, w - barW - tickW - 4);
    const int labelX = 0;
    const int tickX = labelX + labelW;
    const int barX = tickX + tickW + 1;

    // Bar background column (full height, dark).
    p.fillRect(barX, 0, barW, h, QColor("#1c1c1c"));

    // Clip LED above the bar.
    QRect led(barX, 0, barW, clipH - 2);
    bool clipOn = m_clipLatch &&
                  (!m_clipTimer.isValid() || m_clipTimer.elapsed() < kClipHoldMs);
    p.fillRect(led, clipOn ? QColor("#ff3030") : QColor("#3a2020"));

    // Level bar, bottom-anchored: grows up from the bottom on the same dB
    // scale as the ticks.
    float db = linearToDecibels(m_peak);
    float frac = (db - kMinDb) / (kMaxDb - kMinDb);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    int barH = static_cast<int>((h - clipH) * frac);
    if (barH > 0) {
        QLinearGradient grad(0, h, 0, clipH);
        grad.setColorAt(0.0, QColor("#3ec93e")); // green
        grad.setColorAt(0.68, QColor("#d8c428")); // yellow
        grad.setColorAt(0.86, QColor("#e08a2b")); // orange
        grad.setColorAt(1.0, QColor("#e23b3b")); // red
        QRect bar(barX, h - barH, barW, barH);
        p.fillRect(bar, grad);
    }

    // dB scale: minor ticks, then major ticks with labels.
    QFont font = p.font();
    font.setPixelSize(6);
    p.setFont(font);

    for (float tickDb : kMinorTicks) {
        int y = dbToY(tickDb);
        p.fillRect(barX - tickW + 1, y, tickW - 1, 1, QColor("#4a4a4a"));
    }
    for (float tickDb : kMajorTicks) {
        int y = dbToY(tickDb);
        p.fillRect(tickX, y, tickW, 1, QColor("#9a9a9a"));
        QString label = QString::number(static_cast<int>(tickDb));
        p.setPen(QColor("#8a8a8a"));
        p.drawText(QRect(labelX, y - 3, labelW - 1, 7),
                   Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Volume marker: horizontal line across the bar at the fader's dB position.
    if (m_hasVolume) {
        int y = dbToY(m_volumeDb);
        p.fillRect(tickX, y, barX + barW - tickX, 2, QColor("#e6e6e6"));
    }
}
