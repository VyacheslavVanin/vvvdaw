#pragma once
#include <QColor>
#include "RangeRulerWidget.h"

class QPainter;

// Bar/beat ruler: draws beat subdivision lines and numbered bar lines driven
// by tempo + time signature. The loop / record-region dragging and playhead
// marker are inherited.
class MeasureRuler : public RangeRulerWidget {
    Q_OBJECT
public:
    explicit MeasureRuler(QWidget* parent = nullptr);

    void setTempo(double bpm) { m_tempo = bpm; update(); }
    void setTimeSignature(int num, int den) { m_timeSigNum = num; m_timeSigDen = den; update(); }

    double samplesPerBeat() const { return (60.0 / m_tempo) * m_sampleRate; }
    double samplesPerBar() const { return samplesPerBeat() * m_timeSigNum; }

protected:
    QColor backgroundColor() const override { return QColor("#252525"); }
    void paintTicks(QPainter& painter, int64_t startSample, int64_t endSample) override;

private:
    double m_tempo = 120.0;
    int m_timeSigNum = 4;
    int m_timeSigDen = 4;
};
