#include "WaveformPainter.h"
#include <algorithm>
#include <cmath>

QImage WaveformPainter::render(const float* samples, size_t frameCount,
                                int channels, int width, int height,
                                const QColor& color, const QColor& bg) {
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);

    if (!samples || frameCount == 0 || width <= 0 || height <= 0)
        return img;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    int h2 = height / 2;
    int yCenter = h2;

    for (int x = 0; x < width; ++x) {
        int64_t startIdx = static_cast<int64_t>(frameCount) * x / width;
        int64_t endIdx = static_cast<int64_t>(frameCount) * (x + 1) / width;
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

        painter.drawRect(x, yCenter - barHeight, 1, barHeight * 2);
    }
    painter.end();

    return img;
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
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    int h2 = height / 2;
    int yCenter = h2;

    for (int x = 0; x < width; ++x) {
        int64_t startSample = static_cast<int64_t>(totalFrames) * x / width;
        int64_t endSample = static_cast<int64_t>(totalFrames) * (x + 1) / width;
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

        painter.drawRect(x, yCenter - barHeight, 1, barHeight * 2);
    }
    painter.end();

    return img;
}
