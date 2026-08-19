#include "PanSlider.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QString>
#include <cstdlib>

namespace {
constexpr int kGrooveHeight = 5;
constexpr int kGrooveRadius = 3;
constexpr int kHandleWidth = 14;
constexpr int kHandleHeight = 15;
const QColor kTrackColor("#3a3a3a");
const QColor kCenterColor("#9a9a9a");
const QColor kHighlightColor("#6688cc");
const QColor kHandleColor("#cccccc");
} // namespace

PanSlider::PanSlider(QWidget* parent)
    : QSlider(Qt::Horizontal, parent) {
    setRange(-100, 100);
    setValue(0);
    setFixedHeight(kHeight);
    connect(this, &QSlider::valueChanged, this, [this](int) { updateTooltip(); });
    updateTooltip();
}

void PanSlider::updateTooltip() {
    const int min = minimum();
    const int max = maximum();
    const int center = min + (max - min) / 2;
    const int v = value();
    if (v == center) {
        setToolTip("Pan: Center");
        return;
    }
    const int half = (max - min) / 2;
    const int pct = (half > 0) ? std::abs(v - center) * 100 / half : 0;
    setToolTip(v < center ? QString("Pan: L %1%").arg(pct)
                          : QString("Pan: R %1%").arg(pct));
}

QRect PanSlider::grooveRect() const {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    return style()->subControlRect(QStyle::CC_Slider, &opt,
                                   QStyle::SC_SliderGroove, this);
}

int PanSlider::valueX(const QRect& groove, int handleWidth, int value) const {
    const int min = minimum();
    const int max = maximum();
    const double t = (max > min) ? double(value - min) / double(max - min) : 0.0;
    return groove.left() + qRound(t * (groove.width() - handleWidth)) + handleWidth / 2;
}

int PanSlider::centerX(const QRect& groove, int handleWidth) const {
    const int min = minimum();
    const int max = maximum();
    return valueX(groove, handleWidth, min + (max - min) / 2);
}

QRect PanSlider::highlightRect() const {
    const QRect groove = grooveRect();
    const int cx = centerX(groove, kHandleWidth);
    const int vx = valueX(groove, kHandleWidth, value());
    if (vx >= cx)
        return QRect(cx, groove.top(), vx - cx, groove.height());
    return QRect(vx, groove.top(), cx - vx, groove.height());
}

void PanSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect groove = grooveRect();
    const QRect track(groove.left(), groove.center().y() - kGrooveHeight / 2,
                      groove.width(), kGrooveHeight);

    p.setPen(Qt::NoPen);
    p.setBrush(kTrackColor);
    p.drawRoundedRect(track, kGrooveRadius, kGrooveRadius);

    const int cx = centerX(groove, kHandleWidth);

    // Center detent: a short, brighter line marking the middle of the range.
    p.setBrush(kCenterColor);
    p.drawRect(cx - 1, track.top() - 2, 2, track.height() + 4);

    // Highlight from the center to the current value.
    const int vx = valueX(groove, kHandleWidth, value());
    if (vx != cx) {
        QRect hl = track;
        if (vx > cx) {
            hl.setLeft(cx);
            hl.setRight(vx);
        } else {
            hl.setLeft(vx);
            hl.setRight(cx);
        }
        p.setBrush(kHighlightColor);
        p.drawRoundedRect(hl, kGrooveRadius, kGrooveRadius);
    }

    // Handle, centered on the current value.
    const QRect handle(vx - kHandleWidth / 2, track.center().y() - kHandleHeight / 2,
                       kHandleWidth, kHandleHeight);
    p.setBrush(kHandleColor);
    p.drawRoundedRect(handle, kHandleHeight / 2, kHandleHeight / 2);
}