#pragma once
#include <QImage>
#include <QPainter>
#include <QColor>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/AudioClip.h"

// Rasterizes audio waveforms into QImages. All renderers are device-pixel-ratio
// aware: the returned QImage is allocated at `width*devicePixelRatio` physical
// pixels with the DPR set, so drawing it on a HiDPI widget stays crisp.
class WaveformPainter {
public:
    // Per-pixel min/max envelope from raw interleaved samples, for the window
    // [offsetFrame, offsetFrame + visibleFrames). Use below SampleViewZoom.
    static QImage renderSamples(const float* samples, size_t frameCount, int channels,
                                size_t offsetFrame, size_t visibleFrames,
                                int width, int height, double devicePixelRatio,
                                const QColor& color = QColor("#88ccff"),
                                const QColor& bg = Qt::transparent);

    // Sample-by-sample rendering for deep zoom (pixelsPerSample >= 1): every
    // sample of the window is drawn as a distinct vertical tick at its exact x
    // position, so individual samples are discernible.
    static QImage renderSamplesPerSample(const float* samples, size_t frameCount, int channels,
                                         size_t offsetFrame, size_t visibleFrames,
                                         double pixelsPerSample, int width, int height,
                                         double devicePixelRatio,
                                         const QColor& color = QColor("#88ccff"),
                                         const QColor& bg = Qt::transparent);

    // Per-pixel min/max envelope from coarse peaks, for the window
    // [offsetFrame, offsetFrame + visibleFrames) of the clip. Used for
    // streaming clips at overview zoom levels.
    static QImage renderFromPeaks(const AudioClip::Peak* peaks, size_t peakCount,
                                  size_t framesPerPeak, size_t totalFrames,
                                  size_t offsetFrame, size_t visibleFrames,
                                  int width, int height, double devicePixelRatio,
                                  const QColor& color = QColor("#88ccff"),
                                  const QColor& bg = Qt::transparent);

    static QColor defaultColor() { return QColor("#88ccff"); }
    static QColor mutedColor() { return QColor("#555555"); }
    static QColor recordingColor() { return QColor("#ff4444"); }
};