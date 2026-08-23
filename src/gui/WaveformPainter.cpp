#include "WaveformPainter.h"
#include <algorithm>
#include <cmath>
#include <QPolygonF>

namespace {

// Create a device-resolution image and fill the background.
QImage makeImage(int width, int height, double dpr, const QColor& bg) {
    int pw = std::max(1, static_cast<int>(std::ceil(width * dpr)));
    int ph = std::max(1, static_cast<int>(std::ceil(height * dpr)));
    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);
    img.setDevicePixelRatio(dpr);
    return img;
}

// Clamp a y coordinate into the image.
int clampY(int y, int ph) {
    if (y < 0) return 0;
    if (y >= ph) return ph - 1;
    return y;
}

} // namespace

QImage WaveformPainter::renderSamples(const float* samples, size_t frameCount,
                                      int channels, size_t offsetFrame,
                                      size_t visibleFrames,
                                      int width, int height, double dpr,
                                      const QColor& color, const QColor& bg) {
    QImage img = makeImage(width, height, dpr, bg);
    if (!samples || frameCount == 0 || channels <= 0 || width <= 0
        || height <= 0 || visibleFrames == 0 || dpr <= 0.0)
        return img;

    const int pw = img.width();
    const int ph = img.height();

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const double yCenter = ph / 2.0;
    const double amp = ph / 2.0;
    const size_t maxFrame = std::min(offsetFrame + visibleFrames, frameCount);

    for (int px = 0; px < pw; ++px) {
        int lx = static_cast<int>(std::floor(px / dpr));
        if (lx >= width) lx = width - 1;

        int64_t start = static_cast<int64_t>(offsetFrame)
                      + static_cast<int64_t>(visibleFrames) * lx / width;
        int64_t end = static_cast<int64_t>(offsetFrame)
                    + static_cast<int64_t>(visibleFrames) * (lx + 1) / width;
        if (start >= static_cast<int64_t>(maxFrame)) break;
        if (end > static_cast<int64_t>(maxFrame)) end = maxFrame;

        float mn = 0.0f;
        float mx = 0.0f;
        bool first = true;
        for (int64_t i = start; i < end; ++i) {
            float s = samples[i * channels];
            if (first) {
                mn = mx = s;
                first = false;
            } else {
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
        }

        int yTop = clampY(static_cast<int>(std::llround(yCenter - mx * amp)), ph);
        int yBot = clampY(static_cast<int>(std::llround(yCenter - mn * amp)), ph);
        if (yBot >= yTop)
            painter.drawRect(px, yTop, 1, yBot - yTop + 1);
    }
    painter.end();

    return img;
}

QImage WaveformPainter::renderSamplesPerSample(const float* samples, size_t frameCount,
                                               int channels, size_t offsetFrame,
                                               size_t visibleFrames, double pixelsPerSample,
                                               int width, int height, double dpr,
                                               const QColor& color, const QColor& bg) {
    QImage img = makeImage(width, height, dpr, bg);
    if (!samples || frameCount == 0 || channels <= 0 || width <= 0
        || height <= 0 || visibleFrames == 0 || pixelsPerSample <= 0.0 || dpr <= 0.0)
        return img;

    const int ph = img.height();

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const double yCenter = ph / 2.0;
    const double amp = ph / 2.0;

    // Build one connected polyline through every visible sample value so the
    // waveform reads as a single continuous line at deep zoom.
    QPolygonF pts;
    pts.reserve(visibleFrames);
    for (size_t i = 0; i < visibleFrames; ++i) {
        size_t f = offsetFrame + i;
        if (f >= frameCount) break;

        double xlog = static_cast<double>(i) * pixelsPerSample;
        if (xlog >= width) break;

        float s = samples[f * channels];
        pts.append(QPointF(xlog * dpr, yCenter - s * amp));
    }

    const int penW = std::max(1, static_cast<int>(std::ceil(dpr)));
    if (pts.size() >= 2) {
        painter.setPen(QPen(color, penW));
        painter.drawPolyline(pts);
    } else if (pts.size() == 1) {
        painter.setPen(QPen(color, penW));
        painter.drawPoint(pts[0]);
    }
    painter.end();

    return img;
}

QImage WaveformPainter::renderFromPeaks(const AudioClip::Peak* peaks, size_t peakCount,
                                        size_t framesPerPeak, size_t totalFrames,
                                        size_t offsetFrame, size_t visibleFrames,
                                        int width, int height, double dpr,
                                        const QColor& color, const QColor& bg) {
    QImage img = makeImage(width, height, dpr, bg);
    if (!peaks || peakCount == 0 || framesPerPeak == 0 || width <= 0
        || height <= 0 || visibleFrames == 0 || dpr <= 0.0)
        return img;

    const int pw = img.width();
    const int ph = img.height();

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const double yCenter = ph / 2.0;
    const double amp = ph / 2.0;
    const size_t maxFrame = std::min(offsetFrame + visibleFrames, totalFrames);

    for (int px = 0; px < pw; ++px) {
        int lx = static_cast<int>(std::floor(px / dpr));
        if (lx >= width) lx = width - 1;

        int64_t startSample = static_cast<int64_t>(offsetFrame)
                            + static_cast<int64_t>(visibleFrames) * lx / width;
        int64_t endSample = static_cast<int64_t>(offsetFrame)
                          + static_cast<int64_t>(visibleFrames) * (lx + 1) / width;
        if (startSample >= static_cast<int64_t>(maxFrame)) break;
        if (endSample > static_cast<int64_t>(maxFrame)) endSample = maxFrame;

        size_t startPeak = static_cast<size_t>(startSample) / framesPerPeak;
        size_t endPeak = (static_cast<size_t>(endSample) + framesPerPeak - 1) / framesPerPeak;
        if (startPeak >= peakCount) break;
        if (endPeak > peakCount) endPeak = peakCount;

        float mn = 0.0f;
        float mx = 0.0f;
        bool first = true;
        for (size_t i = startPeak; i < endPeak; ++i) {
            float lo = peaks[i].min;
            float hi = peaks[i].max;
            if (first) {
                mn = lo;
                mx = hi;
                first = false;
            } else {
                if (lo < mn) mn = lo;
                if (hi > mx) mx = hi;
            }
        }

        int yTop = clampY(static_cast<int>(std::llround(yCenter - mx * amp)), ph);
        int yBot = clampY(static_cast<int>(std::llround(yCenter - mn * amp)), ph);
        if (yBot >= yTop)
            painter.drawRect(px, yTop, 1, yBot - yTop + 1);
    }
    painter.end();

    return img;
}