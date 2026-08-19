#include "BusColorBar.h"
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QColorDialog>
#include <QApplication>

BusColorBar::BusColorBar(QWidget* parent)
    : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click to assign a color; Ctrl+click overrides child colors");
    m_picker = [](const QColor& initial) {
        return QColorDialog::getColor(initial, nullptr, "Choose bus color");
    };
}

bool BusColorBar::overrideHint() const {
    return m_overrideFromEvent ||
           (QApplication::keyboardModifiers() & Qt::ControlModifier);
}

void BusColorBar::pickColor() {
    const QColor chosen = m_picker ? m_picker(m_color) : m_color;
    if (!chosen.isValid())
        return; // dialog cancelled
    emit colorPicked(chosen, overrideHint());
}

void BusColorBar::resetToAutomaticColor() {
    emit resetToAutomatic(overrideHint());
}

void BusColorBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_color);
    // A subtle top border keeps the bar visible on the strip background.
    painter.setPen(QColor(0, 0, 0, 60));
    painter.drawLine(0, 0, width() - 1, 0);
}

void BusColorBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_overrideFromEvent = (event->modifiers() & Qt::ControlModifier) != 0;
        pickColor();
        m_overrideFromEvent = false;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void BusColorBar::contextMenuEvent(QContextMenuEvent* event) {
    const bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    QMenu menu(this);
    QAction* reset = menu.addAction("Use automatic color");
    connect(reset, &QAction::triggered, this, [this, ctrl] {
        m_overrideFromEvent = ctrl;
        resetToAutomaticColor();
        m_overrideFromEvent = false;
    });
    menu.exec(event->globalPos());
    event->accept();
}