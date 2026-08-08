#pragma once
#include <QColor>
#include "RangeRulerWidget.h"

class QPainter;

// Time ruler: draws second/quarter tick grid with time labels. The loop /
// record-region dragging and playhead marker are inherited.
class TimelineRuler : public RangeRulerWidget {
    Q_OBJECT
public:
    explicit TimelineRuler(QWidget* parent = nullptr);

protected:
    QColor backgroundColor() const override { return QColor("#2a2a2a"); }
    void paintTicks(QPainter& painter, int64_t startSample, int64_t endSample) override;
};
