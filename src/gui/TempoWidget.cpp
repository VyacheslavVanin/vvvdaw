#include "TempoWidget.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

struct SigEntry { int num; int den; };
static const SigEntry s_sigs[] = {
    {2, 2}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}
};
static constexpr int s_sigCount = sizeof(s_sigs) / sizeof(s_sigs[0]);

static constexpr double s_resolutions[] = {1.0, 2.0, 4.0, 8.0, 16.0};
static constexpr int s_resCount = sizeof(s_resolutions) / sizeof(s_resolutions[0]);
static const char* s_resLabels[] = {"1", "1/2", "1/4", "1/8", "1/16"};

TempoWidget::TempoWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* bpmLabel = new QLabel("BPM:", this);
    bpmLabel->setStyleSheet("color: #ccc; font-size: 11px;");
    layout->addWidget(bpmLabel);

    m_bpmSpin = new QDoubleSpinBox(this);
    m_bpmSpin->setRange(20.0, 400.0);
    m_bpmSpin->setValue(120.0);
    m_bpmSpin->setSingleStep(1.0);
    m_bpmSpin->setDecimals(1);
    m_bpmSpin->setFixedWidth(70);
    m_bpmSpin->setFixedHeight(24);
    m_bpmSpin->setStyleSheet(
        "QDoubleSpinBox { background: #3a3a3a; color: #fff; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; }");
    layout->addWidget(m_bpmSpin);

    m_sigCombo = new QComboBox(this);
    for (int i = 0; i < s_sigCount; ++i)
        m_sigCombo->addItem(QString("%1/%2").arg(s_sigs[i].num).arg(s_sigs[i].den));
    m_sigCombo->setCurrentIndex(2); // 4/4
    m_sigCombo->setFixedWidth(64);
    m_sigCombo->setFixedHeight(24);
    m_sigCombo->setStyleSheet(
        "QComboBox { background: #3a3a3a; color: #fff; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #3a3a3a; color: #fff; selection-background-color: #557799; }");
    layout->addWidget(m_sigCombo);

    auto* snapLabel = new QLabel("Snap:", this);
    snapLabel->setStyleSheet("color: #ccc; font-size: 11px;");
    layout->addWidget(snapLabel);

    m_snapCombo = new QComboBox(this);
    for (int i = 0; i < s_resCount; ++i)
        m_snapCombo->addItem(s_resLabels[i]);
    m_snapCombo->setCurrentIndex(2); // 1/4
    m_snapCombo->setFixedWidth(64);
    m_snapCombo->setFixedHeight(24);
    m_snapCombo->setStyleSheet(
        "QComboBox { background: #3a3a3a; color: #fff; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #3a3a3a; color: #fff; selection-background-color: #557799; }");
    layout->addWidget(m_snapCombo);

    m_metronomeBtn = new QPushButton("M", this);
    m_metronomeBtn->setCheckable(true);
    m_metronomeBtn->setToolTip("Metronome");
    m_metronomeBtn->setFixedSize(24, 24);
    m_metronomeBtn->setStyleSheet(
        "QPushButton { background: #3a3a3a; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; font-size: 11px; font-weight: bold; }"
        "QPushButton:checked { background: #335577; color: white; border: 1px solid #5599cc; }");
    layout->addWidget(m_metronomeBtn);

    m_precountBtn = new QPushButton("P", this);
    m_precountBtn->setCheckable(true);
    m_precountBtn->setToolTip("Precount");
    m_precountBtn->setFixedSize(24, 24);
    m_precountBtn->setStyleSheet(
        "QPushButton { background: #3a3a3a; color: #ccc; border: 1px solid #555; "
        "border-radius: 3px; font-size: 11px; font-weight: bold; }"
        "QPushButton:checked { background: #335577; color: white; border: 1px solid #5599cc; }");
    layout->addWidget(m_precountBtn);

    connect(m_metronomeBtn, &QPushButton::toggled, this, &TempoWidget::metronomeToggled);
    connect(m_precountBtn, &QPushButton::toggled, this, &TempoWidget::precountToggled);

    connect(m_bpmSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TempoWidget::tempoChanged);
    connect(m_sigCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < s_sigCount)
            emit timeSignatureChanged(s_sigs[idx].num, s_sigs[idx].den);
    });
    connect(m_snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < s_resCount)
            emit snapResolutionChanged(s_resolutions[idx]);
    });

    setFixedHeight(32);
}

void TempoWidget::setTempo(double bpm) {
    m_bpmSpin->blockSignals(true);
    m_bpmSpin->setValue(bpm);
    m_bpmSpin->blockSignals(false);
}

void TempoWidget::setTimeSignature(int num, int den) {
    for (int i = 0; i < s_sigCount; ++i) {
        if (s_sigs[i].num == num && s_sigs[i].den == den) {
            m_sigCombo->blockSignals(true);
            m_sigCombo->setCurrentIndex(i);
            m_sigCombo->blockSignals(false);
            return;
        }
    }
}

void TempoWidget::setSnapResolution(int index) {
    if (index >= 0 && index < s_resCount) {
        m_snapCombo->blockSignals(true);
        m_snapCombo->setCurrentIndex(index);
        m_snapCombo->blockSignals(false);
    }
}

void TempoWidget::setMetronomeEnabled(bool enabled) {
    m_metronomeBtn->blockSignals(true);
    m_metronomeBtn->setChecked(enabled);
    m_metronomeBtn->blockSignals(false);
}

void TempoWidget::setPrecountEnabled(bool enabled) {
    m_precountBtn->blockSignals(true);
    m_precountBtn->setChecked(enabled);
    m_precountBtn->blockSignals(false);
}
