#pragma once
#include <QSlider>

// A bipolar pan control. Unlike a plain QSlider, whose filled sub-page grows
// from the minimum of the range, this draws a center detent at the middle of
// the range and highlights the span between the center and the current value,
// so the fill shows the deviation from center in either direction. The control
// is also deliberately taller than the compact sliders used elsewhere for
// easier mouse interaction.
class PanSlider : public QSlider {
    Q_OBJECT
public:
    explicit PanSlider(QWidget* parent = nullptr);

    // Widget-space highlight span (from the center of the range to the current
    // value) inside the groove; exposed so tests can verify the center-anchored
    // fill without rendering.
    QRect highlightRect() const;

    static constexpr int kHeight = 16;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect grooveRect() const;
    // X coordinate of the value position (where the handle is centered).
    int valueX(const QRect& groove, int handleWidth, int value) const;
    // X coordinate of the center of the range.
    int centerX(const QRect& groove, int handleWidth) const;
    // Set the tooltip from the current value (e.g. "Pan: L 25%").
    void updateTooltip();
};