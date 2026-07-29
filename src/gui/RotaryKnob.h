#pragma once
#include <QWidget>
#include <QTimer>

class RotaryKnob : public QWidget {
    Q_OBJECT
public:
    explicit RotaryKnob(QWidget* parent = nullptr);

    void setValue(float norm);
    float value() const { return m_value; }
    void setDefaultValue(float val) { m_defaultValue = val; }

signals:
    void valueChanged(float normalized);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    float angleForValue(float norm) const;
    void updateTooltip();

    static constexpr float m_knobSize = 52.0f;
    static constexpr float m_angleMin = 240.0f;
    static constexpr float m_angleRange = 300.0f;

    float m_value = 0.0f;
    float m_defaultValue = 0.0f;
    float m_dragStartValue = 0.0f;
    int m_dragStartY = 0;
    bool m_dragging = false;
    QTimer m_tooltipTimer;
};
