#include "TimelineRuler.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>

TimelineRuler::TimelineRuler(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(24);
}

void TimelineRuler::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#2a2a2a"));

    int64_t sampleRate = 48000;
    double tickInterval = sampleRate;
    if (tickInterval * m_pixelsPerSample < 60) {
        tickInterval *= 4;
    }

    int64_t startSample = m_scrollOffset;
    int64_t endSample = m_scrollOffset + static_cast<int64_t>(width() / m_pixelsPerSample);
    int64_t firstTick = (startSample / static_cast<int64_t>(tickInterval)) * static_cast<int64_t>(tickInterval);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    // Draw minor ticks first
    painter.setPen(QPen(QColor("#555"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;
        for (int i = 1; i < 4; ++i) {
            int mx = x + static_cast<int>((tickInterval / 4) * i * m_pixelsPerSample);
            if (mx >= 0 && mx <= width())
                painter.drawLine(mx, 20, mx, 24);
        }
    }

    // Major ticks + labels
    painter.setPen(QPen(QColor("#aaa"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;

        painter.drawLine(x, 16, x, 24);

        int seconds = static_cast<int>(s / sampleRate);
        int minutes = seconds / 60;
        int secs = seconds % 60;
        QString label = QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
        painter.setPen(QColor("#ccc"));
        painter.drawText(x + 3, 12, label);
        painter.setPen(QPen(QColor("#aaa"), 1));
    }

    // Playhead marker
    if (m_playheadPos >= 0) {
        int phx = static_cast<int>((m_playheadPos - m_scrollOffset) * m_pixelsPerSample);
        if (phx >= 0 && phx <= width()) {
            painter.setPen(QPen(QColor("#ff4444"), 2));
            painter.drawLine(phx, 0, phx, 24);
            // Triangle marker at top
            QPolygonF triangle;
            triangle << QPointF(phx - 4, 0) << QPointF(phx + 4, 0) << QPointF(phx, 6);
            painter.setBrush(QColor("#ff4444"));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
        }
    }
}
