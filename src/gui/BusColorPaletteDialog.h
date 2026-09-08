#pragma once
#include <QDialog>
#include <QColor>
#include <QList>
#include <QString>
#include "core/Constants.h"

class QSlider;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;

// A compact HSV color picker for the track/bus color bars. It exposes a hue
// scale plus adjustable saturation/value (so arbitrary, even non-thematic
// colors are possible), a button that resets saturation/value to the
// automatic tint values of its scheme, and a strip of the last few chosen
// colors. The dialog is accepted with the current color, or rejected on
// cancel → selectedColor() returns an invalid QColor.
class BusColorPaletteDialog : public QDialog {
    Q_OBJECT
public:
    // Which automatic tint values the suggested colors / the S,V reset use,
    // and where the recent colors persist. Defaults match the muted bus
    // scheme; tracks pass the bright AutoTrack* values.
    struct ColorScheme {
        int autoSaturation = vvvdaw::AutoStripSaturation;
        int autoValue = vvvdaw::AutoStripValue;
        QString settingsKey = "bus/colorRecent";
        QString title = "Choose bus color";
    };

    explicit BusColorPaletteDialog(const QColor& initial, QWidget* parent = nullptr);
    explicit BusColorPaletteDialog(const QColor& initial, QWidget* parent,
                                   const ColorScheme& scheme);

    ~BusColorPaletteDialog() override = default;

    // The stepped hue family at the scheme's auto S/V. The no-scheme overload
    // uses the muted bus defaults.
    static QList<QColor> suggestedColors();
    static QList<QColor> suggestedColors(const ColorScheme& scheme);

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

    ColorScheme m_scheme;
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
