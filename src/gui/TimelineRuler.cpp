#include "TimelineRuler.h"
#include "core/Constants.h"
#include <QPainter>

TimelineRuler::TimelineRuler(QWidget* parent)
    : RangeRulerWidget(28, parent)
{
}

void TimelineRuler::paintTicks(QPainter& painter, int64_t startSample, int64_t endSample) {
    double tickInterval = vvvdaw::TickIntervalSamples;
    if (tickInterval * m_pixelsPerSample < 60) {
        tickInterval *= 4;
    }

    int64_t firstTick = (startSample / static_cast<int64_t>(tickInterval)) * static_cast<int64_t>(tickInterval);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    // Minor ticks
    painter.setPen(QPen(QColor("#555"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = sampleToX(s);
        if (x < 0 || x > width()) continue;
        for (int i = 1; i < 4; ++i) {
            int mx = x + static_cast<int>((tickInterval / 4) * i * m_pixelsPerSample);
            if (mx >= 0 && mx <= width())
                painter.drawLine(mx, 20, mx, 28);
        }
    }

    // Major ticks + labels
    painter.setPen(QPen(QColor("#aaa"), 1));
    for (int64_t s = firstTick; s <= endSample; s += static_cast<int64_t>(tickInterval)) {
        int x = sampleToX(s);
        if (x < 0 || x > width()) continue;

        painter.drawLine(x, 16, x, 28);

        int seconds = static_cast<int>(s / m_sampleRate);
        int minutes = seconds / 60;
        int secs = seconds % 60;
        QString label = QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
        painter.setPen(QColor("#ccc"));
        painter.drawText(x + 3, 12, label);
        painter.setPen(QPen(QColor("#aaa"), 1));
    }
}
