#include "TrackColorBar.h"
#include "core/Constants.h"
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>

BusColorPaletteDialog::ColorScheme TrackColorBar::paletteScheme() {
    BusColorPaletteDialog::ColorScheme scheme;
    scheme.autoSaturation = vvvdaw::AutoTrackSaturation;
    scheme.autoValue = vvvdaw::AutoTrackValue;
    scheme.settingsKey = "track/colorRecent";
    scheme.title = "Choose track color";
    return scheme;
}

TrackColorBar::TrackColorBar(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(vvvdaw::TrackColorBarWidth);
    setToolTip("Click to assign a color; right-click to use the bus color");
    m_picker = [](const QColor& initial) {
        BusColorPaletteDialog dialog(initial, nullptr, paletteScheme());
        return (dialog.exec() == QDialog::Accepted) ? dialog.selectedColor()
                                                    : QColor();
    };
}

void TrackColorBar::pickColor() {
    const QColor chosen = m_picker ? m_picker(m_color) : m_color;
    if (!chosen.isValid())
        return; // dialog cancelled
    emit colorPicked(chosen);
}

void TrackColorBar::resetToAutomaticColor() {
    emit resetToAutomatic();
}

void TrackColorBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_color);
    // A subtle right border keeps the bar visible on the row background.
    painter.setPen(QColor(0, 0, 0, 60));
    painter.drawLine(width() - 1, 0, width() - 1, height() - 1);
}

void TrackColorBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pickColor();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TrackColorBar::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* reset = menu.addAction("Use output bus color");
    connect(reset, &QAction::triggered, this, &TrackColorBar::resetToAutomaticColor);
    menu.exec(event->globalPos());
    event->accept();
}
