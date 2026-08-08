#include "MeasureRuler.h"
#include <QPainter>
#include <cmath>

MeasureRuler::MeasureRuler(QWidget* parent)
    : RangeRulerWidget(24, parent)
{
}

void MeasureRuler::paintTicks(QPainter& painter, int64_t startSample, int64_t endSample) {
    double spb = samplesPerBeat();
    double spbar = samplesPerBar();
    if (spb <= 0 || spbar <= 0) return;

    // Adaptive beat subdivision based on zoom level
    double pixelsPerBeat = spb * m_pixelsPerSample;
    int subDivision;
    if (pixelsPerBeat > 80)
        subDivision = 1;
    else if (pixelsPerBeat > 40)
        subDivision = 2;
    else if (pixelsPerBeat > 10)
        subDivision = 4;
    else
        subDivision = 8;

    double subBeatSamples = spb / (4.0 / subDivision);
    if (subBeatSamples <= 0) return;

    int64_t firstSubSample = static_cast<int64_t>(
        std::ceil(startSample / subBeatSamples) * subBeatSamples);

    // Beat / subdivision lines
    painter.setPen(QPen(QColor("#555"), 1));
    for (int64_t s = firstSubSample; s <= endSample; s += static_cast<int64_t>(subBeatSamples)) {
        int x = sampleToX(s);
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
        int x = sampleToX(s);
        if (x < 0 || x > width()) continue;

        painter.setPen(QPen(QColor("#aaccdd"), 2));
        painter.drawLine(x, 0, x, 24);

        int barNum = static_cast<int>(s / spbar) + 1;
        painter.setPen(QColor("#aaccdd"));
        painter.drawText(x + 3, 11, QString::number(barNum));
    }
}
