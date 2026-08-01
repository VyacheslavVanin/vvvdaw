#include "PluginParameterWidget.h"
#include "PluginWindow.h"
#include "RotaryKnob.h"
#include "plugin/PluginInstance.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QScrollArea>
#include <cmath>

PluginParameterWidget::PluginParameterWidget(PluginInstance* plugin,
                                             int knobsPerRow,
                                             QWidget* parent)
    : QWidget(parent)
    , m_plugin(plugin) {
    if (!m_plugin) return;

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget();
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(8, 8, 8, 8);
    containerLayout->setSpacing(6);

    auto ports = m_plugin->ports();

    // --- Control ports → knob grid ---
    int cols = std::max(2, knobsPerRow);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);

    int knobIdx = 0;
    for (const auto& port : ports) {
        if (port.direction != PluginPortInfo::Direction::Input) continue;
        if (port.type != PluginPortInfo::Type::Control) continue;

        int col = knobIdx % cols;
        int row = (knobIdx / cols) * 3;

        float range = port.maxValue - port.minValue;
        float minVal = port.minValue;
        float currentNorm = range > 0.0f
            ? (m_plugin->getParameter(port.index) - minVal) / range
            : 0.0f;

        auto* knob = new RotaryKnob(this);
        knob->setValue(std::clamp(currentNorm, 0.0f, 1.0f));
        knob->setDefaultValue(
            range > 0.0f
                ? (port.defaultValue - minVal) / range
                : 0.0f);

        auto* valLabel = new QLabel("0.00");
        valLabel->setAlignment(Qt::AlignCenter);
        valLabel->setFixedHeight(14);
        valLabel->setStyleSheet("color: #aaa; font-size: 9px;");

        auto* nameLabel = new QLabel(port.name);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setFixedHeight(14);
        nameLabel->setStyleSheet("color: #ccc; font-size: 9px;");

        float display = minVal + currentNorm * range;
        valLabel->setText(QString::number(display, 'f', 2));

        int capturedIndex = port.index;
        connect(knob, &RotaryKnob::valueChanged, this,
                [this, capturedIndex, minVal, range, valLabel](float norm) {
            float actualValue = minVal + norm * range;
            float oldValue = m_plugin->getParameter(capturedIndex);
            if (oldValue != actualValue) {
                m_plugin->setParameter(capturedIndex, actualValue);
                emit parameterChangeRequested(capturedIndex, oldValue, actualValue);
            }
            float display = minVal + norm * range;
            valLabel->setText(QString::number(display, 'f', 2));
        });

        grid->addWidget(knob, row, col, Qt::AlignCenter);
        grid->addWidget(nameLabel, row + 1, col, Qt::AlignCenter);
        grid->addWidget(valLabel, row + 2, col, Qt::AlignCenter);

        m_knobs.push_back(knob);
        m_paramIndices.push_back(port.index);
        m_knobRanges.push_back({minVal, range, valLabel});
        ++knobIdx;
    }

    if (knobIdx > 0)
        containerLayout->addLayout(grid);

    // --- Path / String / Atom ports → horizontal rows ---
    for (const auto& port : ports) {
        if (port.direction != PluginPortInfo::Direction::Input) continue;
        if (port.type != PluginPortInfo::Type::Path &&
            port.type != PluginPortInfo::Type::String &&
            port.type != PluginPortInfo::Type::Atom) continue;

        auto* row = new QHBoxLayout();
        row->setSpacing(8);

        auto* nameLabel = new QLabel(port.name);
        nameLabel->setMinimumWidth(120);
        nameLabel->setStyleSheet("color: #ccc; font-size: 11px;");
        row->addWidget(nameLabel);

        auto* pathEdit = new QLineEdit;
        pathEdit->setReadOnly(false);
        pathEdit->setText(m_plugin->getStringParameter(port.index));
        pathEdit->setStyleSheet(
            "QLineEdit { background: #333; color: #ccc; border: 1px solid #555; "
            "padding: 3px; border-radius: 2px; }"
        );
        row->addWidget(pathEdit, 1);

        int capturedIndex = port.index;
        bool isPathLike = (port.type == PluginPortInfo::Type::Path ||
                           port.type == PluginPortInfo::Type::Atom);
        if (isPathLike) {
            auto* browseBtn = new QPushButton("Browse...");
            browseBtn->setStyleSheet(
                "QPushButton { background: #444; color: #ccc; border: 1px solid #666; "
                "padding: 3px 8px; border-radius: 2px; }"
                "QPushButton:hover { background: #555; }"
            );
            connect(browseBtn, &QPushButton::clicked, this, [this, pathEdit, capturedIndex]() {
                QString path = QFileDialog::getOpenFileName(this, tr("Select file"));
                if (!path.isEmpty()) {
                    QString oldValue = m_plugin->getStringParameter(capturedIndex);
                    pathEdit->setText(path);
                    m_plugin->setStringParameter(capturedIndex, path);
                    emit pathParameterChangeRequested(capturedIndex, oldValue, path);
                }
            });
            row->addWidget(browseBtn);

            connect(pathEdit, &QLineEdit::returnPressed, this, [this, pathEdit, capturedIndex]() {
                QString path = pathEdit->text();
                QString oldValue = m_plugin->getStringParameter(capturedIndex);
                if (oldValue != path) {
                    m_plugin->setStringParameter(capturedIndex, path);
                    emit pathParameterChangeRequested(capturedIndex, oldValue, path);
                }
            });
        } else {
            connect(pathEdit, &QLineEdit::editingFinished, this, [this, pathEdit, capturedIndex]() {
                QString val = pathEdit->text();
                QString oldValue = m_plugin->getStringParameter(capturedIndex);
                if (oldValue != val) {
                    m_plugin->setStringParameter(capturedIndex, val);
                    emit pathParameterChangeRequested(capturedIndex, oldValue, val);
                }
            });
        }

        containerLayout->addLayout(row);

        m_stringParams.push_back({port.index, pathEdit});
    }

    containerLayout->addStretch();
    scrollArea->setWidget(container);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scrollArea);

    setStyleSheet("background: #2a2a2a;");

    // Set callback so undo/redo can update knobs
    m_plugin->setParamValueCallback([this](int paramIndex, float value) {
        for (size_t i = 0; i < m_paramIndices.size(); ++i) {
            if (m_paramIndices[i] == paramIndex) {
                auto& kr = m_knobRanges[i];
                float norm = kr.range > 0.0f
                    ? std::clamp((value - kr.min) / kr.range, 0.0f, 1.0f)
                    : 0.0f;
                m_knobs[i]->setValue(norm);
                if (kr.label)
                    kr.label->setText(QString::number(value, 'f', 2));
                break;
            }
        }
    });

    // Set callback so undo/redo can update string/path parameter fields
    m_plugin->setStringParamValueCallback([this](int paramIndex, const QString& value) {
        for (auto& sp : m_stringParams) {
            if (sp.paramIndex == paramIndex && sp.edit) {
                sp.edit->setText(value);
                break;
            }
        }
    });
    // Null out plugin pointer when parent window closes, before plugin is destroyed.
    // Clear value callbacks first so the plugin never holds dangling widget pointers.
    if (auto* pw = qobject_cast<PluginWindow*>(parentWidget())) {
        connect(pw, &PluginWindow::windowClosed, this, [this]() {
            if (m_plugin) {
                m_plugin->setParamValueCallback(nullptr);
                m_plugin->setStringParamValueCallback(nullptr);
            }
            m_plugin = nullptr;
        });
    }
}

PluginParameterWidget::~PluginParameterWidget() {
    if (m_plugin) {
        m_plugin->setParamValueCallback(nullptr);
        m_plugin->setStringParamValueCallback(nullptr);
    }
}
