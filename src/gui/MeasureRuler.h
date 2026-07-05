#pragma once
#include <QWidget>
#include <algorithm>
#include <cstdint>
#include "core/Constants.h"

class MeasureRuler : public QWidget {
    Q_OBJECT
public:
    explicit MeasureRuler(QWidget* parent = nullptr);

    void setScrollOffset(int64_t offset) { m_scrollOffset = offset; update(); }
    int64_t scrollOffset() const { return m_scrollOffset; }

    void setZoom(double pixelsPerSample) {
        m_pixelsPerSample = std::clamp(pixelsPerSample, vvvdaw::MinZoom, vvvdaw::MaxZoom);
        update();
    }
    double zoom() const { return m_pixelsPerSample; }

    void setTempo(double bpm) { m_tempo = bpm; update(); }
    void setTimeSignature(int num, int den) { m_timeSigNum = num; m_timeSigDen = den; update(); }
    void setSampleRate(int rate) { m_sampleRate = rate; update(); }

    double samplesPerBeat() const { return (60.0 / m_tempo) * m_sampleRate; }
    double samplesPerBar() const { return samplesPerBeat() * m_timeSigNum; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int64_t m_scrollOffset = 0;
    double m_pixelsPerSample = 0.001;
    double m_tempo = 120.0;
    int m_timeSigNum = 4;
    int m_timeSigDen = 4;
    int m_sampleRate = vvvdaw::DefaultSampleRate;
};
