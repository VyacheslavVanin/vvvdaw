#include "WaveformPainter.h"
#include <algorithm>
#include <cmath>
#include <QDebug>

QImage WaveformPainter::render(const float* samples, size_t frameCount,
                                int channels, int width, int height,
                                const QColor& color, const QColor& bg) {
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);

    if (!samples || frameCount == 0 || width <= 0 || height <= 0)
        return img;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    paint(painter, img.rect(), samples, frameCount, channels, 0, frameCount, color);
    painter.end();

    return img;
}

void WaveformPainter::paint(QPainter& painter, const QRect& rect,
                             const float* samples, size_t frameCount,
                             int channels, int64_t offsetSamples,
                             int64_t visibleSamples, const QColor& color) {
    if (!samples || frameCount == 0 || rect.width() <= 0 || rect.height() <= 0)
        return;

    if (visibleSamples == 0) visibleSamples = frameCount;

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    int h2 = rect.height() / 2;
    int yCenter = rect.y() + h2;

    for (int x = 0; x < rect.width(); ++x) {
        int64_t startIdx = offsetSamples + (visibleSamples * x) / rect.width();
        int64_t endIdx = offsetSamples + (visibleSamples * (x + 1)) / rect.width();
        if (startIdx >= static_cast<int64_t>(frameCount)) break;
        if (endIdx > static_cast<int64_t>(frameCount)) endIdx = frameCount;

        float maxVal = 0.0f;
        for (int64_t i = startIdx; i < endIdx; ++i) {
            float s = std::abs(samples[i * channels]);
            if (s > maxVal) maxVal = s;
        }
        maxVal = std::min(maxVal, 1.0f);

        int barHeight = static_cast<int>(maxVal * h2);
        if (barHeight < 1) barHeight = 1;

        painter.drawRect(rect.x() + x, yCenter - barHeight, 1, barHeight * 2);
    }
}

QImage WaveformPainter::renderFromPeaks(const AudioClip::Peak* peaks, size_t peakCount,
                                          size_t framesPerPeak, size_t totalFrames,
                                          int width, int height,
                                          const QColor& color, const QColor& bg) {
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);

    if (!peaks || peakCount == 0 || width <= 0 || height <= 0)
        return img;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    paintPeaks(painter, img.rect(), peaks, peakCount, framesPerPeak, totalFrames,
               0, totalFrames, color);
    painter.end();

    return img;
}

void WaveformPainter::paintPeaks(QPainter& painter, const QRect& rect,
                                  const AudioClip::Peak* peaks, size_t peakCount,
                                  size_t framesPerPeak, size_t totalFrames,
                                  int64_t offsetSamples, int64_t visibleSamples,
                                  const QColor& color) {
    if (!peaks || peakCount == 0 || rect.width() <= 0 || rect.height() <= 0)
        return;

    if (visibleSamples == 0) visibleSamples = totalFrames;

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    int h2 = rect.height() / 2;
    int yCenter = rect.y() + h2;

    for (int x = 0; x < rect.width(); ++x) {
        int64_t startSample = offsetSamples + (visibleSamples * x) / rect.width();
        int64_t endSample = offsetSamples + (visibleSamples * (x + 1)) / rect.width();
        if (startSample >= static_cast<int64_t>(totalFrames)) break;
        if (endSample > static_cast<int64_t>(totalFrames)) endSample = totalFrames;

        size_t startPeak = static_cast<size_t>(startSample) / framesPerPeak;
        size_t endPeak = (static_cast<size_t>(endSample) + framesPerPeak - 1) / framesPerPeak;
        if (startPeak >= peakCount) break;
        if (endPeak > peakCount) endPeak = peakCount;

        float maxVal = 0.0f;
        for (size_t i = startPeak; i < endPeak; ++i)
            if (peaks[i].maxAbs > maxVal) maxVal = peaks[i].maxAbs;

        maxVal = std::min(maxVal, 1.0f);
        int barHeight = static_cast<int>(maxVal * h2);
        if (barHeight < 1) barHeight = 1;

        painter.drawRect(rect.x() + x, yCenter - barHeight, 1, barHeight * 2);
    }
}
