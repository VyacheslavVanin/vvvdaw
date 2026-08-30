#pragma once
#include <QDialog>
#include <QColor>
#include <QList>
#include "core/Constants.h"

class QSlider;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;

// A compact HSV color picker for the bus color bar. It exposes a hue scale plus
// adjustable saturation/value (so arbitrary, even non-thematic colors are
// possible), a button that resets saturation/value to the automatic bus tint
// values, and a strip of the last few chosen colors. The dialog is accepted
// with the current color, or rejected on cancel → selectedColor() returns an
// invalid QColor.
class BusColorPaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit BusColorPaletteDialog(const QColor& initial, QWidget* parent = nullptr);

    ~BusColorPaletteDialog() override = default;

    // The muted tints matching the automatic bus scheme (used to seed the
    // recent-colors row on first use).
    static QList<QColor> suggestedColors();

    // The color chosen by the user; invalid if the dialog was cancelled.
    QColor selectedColor() const { return m_current; }

private:
    QColor currentColor() const;
    void applyInitial(const QColor& color);
    void updatePreview();
    void rebuildRecent();
    void recordRecent(const QColor& color);
    void loadRecent();
    void saveRecent();
    QHBoxLayout* makeSliderRow(const QString& text, QSlider*& slider, int min, int max,
                               QLabel*& valueLabel);

    QColor m_current;
    int m_h = 0;
    int m_s = vvvdaw::AutoStripSaturation;
    int m_v = vvvdaw::AutoStripValue;

    QSlider* m_hueSlider = nullptr;
    QSlider* m_saturationSlider = nullptr;
    QSlider* m_valueSlider = nullptr;
    QLabel* m_hueValueLabel = nullptr;
    QLabel* m_saturationValueLabel = nullptr;
    QLabel* m_valueValueLabel = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_hexLabel = nullptr;
    QHBoxLayout* m_recentLayout = nullptr;

    QList<QColor> m_recent;

    static constexpr int kRecentCount = 9;
};
