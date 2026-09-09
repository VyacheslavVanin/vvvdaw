#pragma once
#include <QColor>
#include <QtGlobal>
#include <cmath>

// Contrast helper for colored event surfaces: the audio waveform and the MIDI
// note fills must stay readable on the (often bright, saturated) event
// background. The decision is based on the WCAG relative luminance of the
// background: vivid mid-brightness tints read better with dark content; very
// dark ones keep the light default content color.
inline qreal relativeLuminance(const QColor& c) {
    const auto lin = [](qreal v) {
        return v <= qreal(0.03928) ? v / qreal(12.92)
                                   : std::pow((v + qreal(0.055)) / qreal(1.055), qreal(2.4));
    };
    return qreal(0.2126) * lin(c.redF()) + qreal(0.7152) * lin(c.greenF())
         + qreal(0.0722) * lin(c.blueF());
}

// The crossover luminance where dark content (#1a1a1a) and the light default
// content have equal contrast against the background (~2.8:1 on both sides).
inline constexpr qreal kDarkContentLuminance = 0.12;

inline bool useDarkContent(const QColor& bg) {
    return relativeLuminance(bg) > kDarkContentLuminance;
}

inline QColor contentColorFor(const QColor& bg, const QColor& fallback) {
    return useDarkContent(bg) ? QColor("#1a1a1a") : fallback;
}
