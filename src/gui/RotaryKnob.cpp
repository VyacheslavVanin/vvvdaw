#include "RotaryKnob.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QAction>
#include <cmath>
#include <QApplication>
#include <QStyle>
#include <QToolTip>

RotaryKnob::RotaryKnob(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(56, 56);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);

    m_tooltipTimer.setSingleShot(true);
    m_tooltipTimer.setInterval(400);
    connect(&m_tooltipTimer, &QTimer::timeout, this, &RotaryKnob::updateTooltip);
}

float RotaryKnob::angleForValue(float norm) const {
    // Arc sweeps clockwise from m_angleMin, so angle decreases with value
    return m_angleMin - norm * m_angleRange;
}

void RotaryKnob::setValue(float norm) {
    m_value = std::clamp(norm, 0.0f, 1.0f);
    update();
}

void RotaryKnob::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const float cx = width() / 2.0f;
    const float cy = height() / 2.0f;
    const float r = m_knobSize / 2.0f - 4.0f;
    const float arcWidth = 2.5f;

    // Background arc track
    QPen bgPen(QColor(58, 58, 58), arcWidth, Qt::SolidLine, Qt::FlatCap);
    p.setPen(bgPen);
    p.drawArc(QRectF(cx - r, cy - r, r * 2, r * 2),
              static_cast<int>(m_angleMin * 16),
              static_cast<int>(-m_angleRange * 16));

    // Value arc
    float valAngle = angleForValue(m_value);
    float valSweep = m_angleMin - valAngle;
    QPen valPen(QColor(85, 153, 204), arcWidth, Qt::SolidLine, Qt::RoundCap);
    p.setPen(valPen);
    if (valSweep > 0.5f)
        p.drawArc(QRectF(cx - r, cy - r, r * 2, r * 2),
                  static_cast<int>(m_angleMin * 16),
                  static_cast<int>(-valSweep * 16));

    // Tick marks at min and max
    float tickInner = r - 5.0f;
    float tickOuter = r + 3.0f;
    QPen tickPen(QColor(102, 102, 102), 1.0f, Qt::SolidLine, Qt::RoundCap);
    p.setPen(tickPen);

    auto drawTick = [&](float angle) {
        float rad = -angle * static_cast<float>(M_PI) / 180.0f;
        float cosA = std::cos(rad);
        float sinA = std::sin(rad);
        p.drawLine(QPointF(cx + tickInner * cosA, cy + tickInner * sinA),
                   QPointF(cx + tickOuter * cosA, cy + tickOuter * sinA));
    };
    drawTick(m_angleMin);
    drawTick(std::fmod(m_angleMin - m_angleRange + 360.0f, 360.0f));

    // Center body
    float bodyR = r * 0.42f;
    p.setPen(QPen(QColor(68, 68, 68), 1.0f));
    p.setBrush(QColor(42, 42, 42));
    p.drawEllipse(QPointF(cx, cy), bodyR, bodyR);

    // Pointer line
    float pInner = bodyR + 1.0f;
    float pOuter = r - 3.0f;
    float angRad = -valAngle * static_cast<float>(M_PI) / 180.0f;
    float cosA = std::cos(angRad);
    float sinA = std::sin(angRad);
    QPen ptrPen(QColor(200, 200, 200), 1.5f, Qt::SolidLine, Qt::RoundCap);
    p.setPen(ptrPen);
    p.drawLine(QPointF(cx + pInner * cosA, cy + pInner * sinA),
               QPointF(cx + pOuter * cosA, cy + pOuter * sinA));
}

void RotaryKnob::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartY = event->position().y();
        m_dragStartValue = m_value;
        m_tooltipTimer.start();
    }
    QWidget::mousePressEvent(event);
}

void RotaryKnob::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        int delta = m_dragStartY - event->position().y();
        float sensitivity = QApplication::keyboardModifiers() & Qt::ShiftModifier ? 0.002f : 0.005f;
        float val = std::clamp(m_dragStartValue + delta * sensitivity, 0.0f, 1.0f);
        if (val != m_value) {
            m_value = val;
            update();
            emit valueChanged(m_value);
            m_tooltipTimer.stop();
            QToolTip::showText(event->globalPosition().toPoint(),
                               QString::number(m_value, 'f', 3), this);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void RotaryKnob::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_tooltipTimer.stop();
        QToolTip::hideText();
    }
    QWidget::mouseReleaseEvent(event);
}

void RotaryKnob::wheelEvent(QWheelEvent* event) {
    float step = QApplication::keyboardModifiers() & Qt::ShiftModifier ? 0.005f : 0.05f;
    float val = std::clamp(m_value + (event->angleDelta().y() > 0 ? step : -step), 0.0f, 1.0f);
    if (val != m_value) {
        m_value = val;
        update();
        emit valueChanged(m_value);
    }
    event->accept();
}

void RotaryKnob::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_value = m_defaultValue;
        update();
        emit valueChanged(m_value);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void RotaryKnob::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #333; color: #ccc; border: 1px solid #555; font-size: 11px; }"
        "QMenu::item:selected { background: #094771; }"
    );
    QAction* resetAct = menu.addAction("Reset to Default");
    connect(resetAct, &QAction::triggered, this, [this]() {
        m_value = m_defaultValue;
        update();
        emit valueChanged(m_value);
    });
    menu.exec(event->globalPos());
}

void RotaryKnob::updateTooltip() {
    QPoint pos = mapToGlobal(QPoint(width() / 2, -24));
    QToolTip::showText(pos, QString::number(m_value, 'f', 3), this);
}
