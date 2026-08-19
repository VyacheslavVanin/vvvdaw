#pragma once
#include <QWidget>
#include <QColor>
#include <functional>
#include <QDebug>

// The thin full-width strip at the bottom of a bus strip. It shows the bus's
// effective color and opens a color picker on click. Holding Ctrl while
// clicking assigns the color to the bus and overrides the colors of all its
// child buses; a right-click menu restores the automatic color.
class BusColorBar : public QWidget {
    Q_OBJECT
public:
    explicit BusColorBar(QWidget* parent = nullptr);

    void setColor(const QColor& color) { m_color = color; update(); }
    QColor color() const { return m_color; }

    // Picker used when the bar is clicked. The default opens a modal
    // QColorDialog; tests inject a stub to avoid a blocking dialog. A returned
    // invalid QColor means "cancel".
    using ColorPicker = std::function<QColor(const QColor& initial)>;
    void setColorPickerForTesting(ColorPicker picker) { m_picker = std::move(picker); }

public slots:
    // Open the picker and emit colorPicked with the chosen color (if any).
    // Honors a pending Ctrl modifier from the triggering mouse event.
    void pickColor();
    // Restore the automatic color (emit resetToAutomatic).
    void resetToAutomaticColor();

signals:
    void colorPicked(const QColor& color, bool overrideChildren);
    void resetToAutomatic(bool overrideChildren);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    bool overrideHint() const;

    QColor m_color;
    ColorPicker m_picker;
    bool m_overrideFromEvent = false;
};