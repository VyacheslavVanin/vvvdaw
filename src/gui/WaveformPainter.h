#pragma once
#include <QImage>
#include <QPainter>
#include <QColor>
#include <vector>

class WaveformPainter {
public:
    static QImage render(const float* samples, size_t frameCount, int channels,
                         int width, int height,
                         const QColor& color = QColor("#88ccff"),
                         const QColor& bg = Qt::transparent);

    static void paint(QPainter& painter, const QRect& rect,
                      const float* samples, size_t frameCount, int channels,
                      int64_t offsetSamples, int64_t visibleSamples,
                      const QColor& color = QColor("#88ccff"));

    static QColor defaultColor() { return QColor("#88ccff"); }
    static QColor mutedColor() { return QColor("#555555"); }
    static QColor recordingColor() { return QColor("#ff4444"); }
};
