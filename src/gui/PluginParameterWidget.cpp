#include "PluginParameterWidget.h"
#include "plugin/PluginInstance.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QScrollArea>
#include <cmath>

PluginParameterWidget::PluginParameterWidget(PluginInstance* plugin, QWidget* parent)
    : QWidget(parent)
    , m_plugin(plugin) {
    if (!m_plugin) return;

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto ports = m_plugin->ports();
    int paramIdx = 0;
    for (const auto& port : ports) {
        if (port.type != PluginPortInfo::Type::Control || port.direction != PluginPortInfo::Direction::Input)
            continue;

        m_paramIndices.push_back(port.index);

        auto* row = new QHBoxLayout();
        row->setSpacing(8);

        auto* nameLabel = new QLabel(port.name);
        nameLabel->setMinimumWidth(120);
        nameLabel->setStyleSheet("color: #ccc; font-size: 11px;");
        row->addWidget(nameLabel);

        auto* slider = new QSlider(Qt::Horizontal);
        slider->setMinimumWidth(150);
        slider->setRange(0, 1000);

        float current = m_plugin->getParameter(port.index);
        slider->setValue(static_cast<int>(current * 1000.0f));
        slider->setStyleSheet(
            "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #5599cc; width: 12px; margin: -4px 0; border-radius: 6px; }"
        );

        int capturedIndex = port.index;
        float range = port.maxValue - port.minValue;
        float minVal = port.minValue;
        connect(slider, &QSlider::valueChanged, this, [this, capturedIndex, range, minVal](int value) {
            float norm = value / 1000.0f;
            onSliderChanged(capturedIndex, norm);
        });
        row->addWidget(slider, 1);

        auto* valueLabel = new QLabel(QString::number(port.defaultValue, 'f', 2));
        valueLabel->setMinimumWidth(50);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setStyleSheet("color: #aaa; font-size: 11px;");
        row->addWidget(valueLabel);

        float val = minVal + current * range;
        valueLabel->setText(QString::number(val, 'f', 2));

        connect(slider, &QSlider::valueChanged, this, [valueLabel, range, minVal](int value) {
            float norm = value / 1000.0f;
            float display = minVal + norm * range;
            valueLabel->setText(QString::number(display, 'f', 2));
        });

        layout->addLayout(row);
        paramIdx++;
    }

    layout->addStretch();
    scrollArea->setWidget(container);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scrollArea);

    setStyleSheet("background: #2a2a2a;");
    setMinimumSize(350, qMax(100, paramIdx * 36 + 20));
}

void PluginParameterWidget::onSliderChanged(int paramIndex, float normalizedValue) {
    if (m_plugin)
        m_plugin->setParameter(paramIndex, normalizedValue);
}
