#include "MeasureRuler.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>

MeasureRuler::MeasureRuler(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(24);
}

void MeasureRuler::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#252525"));

    double spb = samplesPerBeat();
    double spbar = samplesPerBar();
    if (spb <= 0 || spbar <= 0) return;

    // Adaptive beat subdivision based on zoom level
    double pixelsPerBeat = spb * m_pixelsPerSample;
    int subDivision;
    if (pixelsPerBeat > 80)
        subDivision = 1;     // sixteenth
    else if (pixelsPerBeat > 40)
        subDivision = 2;     // eighth
    else if (pixelsPerBeat > 10)
        subDivision = 4;     // quarter
    else
        subDivision = 8;     // half beat step

    int64_t startSample = m_scrollOffset;
    int64_t endSample = m_scrollOffset + static_cast<int64_t>(width() / m_pixelsPerSample);

    double subBeatSamples = spb / (4.0 / subDivision);
    if (subBeatSamples <= 0) return;

    // Find first beat position
    int64_t firstBeatSample = static_cast<int64_t>(
        std::ceil(startSample / spb) * spb);
    int64_t firstSubSample = static_cast<int64_t>(
        std::ceil(startSample / subBeatSamples) * subBeatSamples);

    // Beat / subdivision lines
    painter.setPen(QPen(QColor("#555"), 1));
    for (int64_t s = firstSubSample; s <= endSample; s += static_cast<int64_t>(subBeatSamples)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;

        bool isBeat = (s % static_cast<int64_t>(spb) == 0);
        if (isBeat)
            painter.setPen(QPen(QColor("#7799bb"), 2));
        else
            painter.setPen(QPen(QColor("#444"), 1));

        painter.drawLine(x, 8, x, 24);
    }

    // Bar lines + labels
    QFont font = painter.font();
    font.setPixelSize(9);
    painter.setFont(font);

    int64_t firstBarSample = static_cast<int64_t>(
        std::ceil(startSample / spbar) * spbar);

    for (int64_t s = firstBarSample; s <= endSample; s += static_cast<int64_t>(spbar)) {
        int x = static_cast<int>((s - m_scrollOffset) * m_pixelsPerSample);
        if (x < 0 || x > width()) continue;

        painter.setPen(QPen(QColor("#aaccdd"), 2));
        painter.drawLine(x, 0, x, 24);

        int barNum = static_cast<int>(s / spbar) + 1;
        painter.setPen(QColor("#aaccdd"));
        painter.drawText(x + 3, 11, QString::number(barNum));
    }
}
