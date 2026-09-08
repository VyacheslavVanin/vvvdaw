#pragma once
#include <QWidget>
#include <QColor>
#include <functional>
#include "BusColorPaletteDialog.h"

// The thin vertical strip on the left edge of a track row. It shows the
// track's effective color (manual, or inherited from its output bus) and
// opens a color picker on left-click; a right-click menu restores the
// automatic color (follow the output bus).
class TrackColorBar : public QWidget {
    Q_OBJECT
public:
    explicit TrackColorBar(QWidget* parent = nullptr);

    // The picker scheme for tracks: bright AutoTrack* saturation/value,
    // separate recents key, track-specific title.
    static BusColorPaletteDialog::ColorScheme paletteScheme();

    void setColor(const QColor& color) { m_color = color; update(); }
    QColor color() const { return m_color; }

    // Picker used when the bar is clicked. The default opens a modal palette
    // dialog; tests inject a stub to avoid a blocking dialog. A returned
    // invalid QColor means "cancel".
    using ColorPicker = std::function<QColor(const QColor& initial)>;
    void setColorPickerForTesting(ColorPicker picker) { m_picker = std::move(picker); }

public slots:
    // Open the picker and emit colorPicked with the chosen color (if any).
    void pickColor();
    // Restore the automatic color (emit resetToAutomatic).
    void resetToAutomaticColor();

signals:
    void colorPicked(const QColor& color);
    void resetToAutomatic();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QColor m_color;
    ColorPicker m_picker;
};
