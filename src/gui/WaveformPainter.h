#pragma once
#include <QImage>
#include <QPainter>
#include <QColor>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/AudioClip.h"

class WaveformPainter {
public:
    static QImage render(const float* samples, size_t frameCount, int channels,
                         int width, int height,
                         const QColor& color = QColor("#88ccff"),
                         const QColor& bg = Qt::transparent);

    static QImage renderFromPeaks(const AudioClip::Peak* peaks, size_t peakCount,
                                   size_t framesPerPeak, size_t totalFrames,
                                   int width, int height,
                                   const QColor& color = QColor("#88ccff"),
                                   const QColor& bg = Qt::transparent);

    static void paint(QPainter& painter, const QRect& rect,
                      const float* samples, size_t frameCount, int channels,
                      int64_t offsetSamples, int64_t visibleSamples,
                      const QColor& color = QColor("#88ccff"));

    static void paintPeaks(QPainter& painter, const QRect& rect,
                           const AudioClip::Peak* peaks, size_t peakCount,
                           size_t framesPerPeak, size_t totalFrames,
                           int64_t offsetSamples, int64_t visibleSamples,
                           const QColor& color = QColor("#88ccff"));

    static QColor defaultColor() { return QColor("#88ccff"); }
    static QColor mutedColor() { return QColor("#555555"); }
    static QColor recordingColor() { return QColor("#ff4444"); }
};
