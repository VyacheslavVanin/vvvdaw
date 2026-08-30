#include "BusColorPaletteDialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QPushButton>
#include <QSettings>

QList<QColor> BusColorPaletteDialog::suggestedColors() {
    QList<QColor> palette;
    // The same stepped hue family as the automatic bus tint (Project::folderColorFor),
    // but at the darker strip values so the swatches match what auto buses look like.
    for (int i = 0; i < 9; ++i)
        palette.append(QColor::fromHsv((i * 47) % 360,
                                       vvvdaw::AutoStripSaturation, vvvdaw::AutoStripValue));
    return palette;
}

BusColorPaletteDialog::BusColorPaletteDialog(const QColor& initial, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Choose bus color");
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Header: live preview swatch + current hex value.
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(28, 28);
    m_preview->setStyleSheet("border: 1px solid #555; border-radius: 4px;");
    header->addWidget(m_preview);
    m_hexLabel = new QLabel(this);
    m_hexLabel->setMinimumWidth(64);
    header->addWidget(m_hexLabel);
    header->addStretch(1);
    root->addLayout(header);

    // Hue / saturation / value scales.
    root->addLayout(makeSliderRow("Hue", m_hueSlider, 0, 359, m_hueValueLabel));
    root->addLayout(makeSliderRow("Sat", m_saturationSlider, 0, 255, m_saturationValueLabel));
    root->addLayout(makeSliderRow("Val", m_valueSlider, 0, 255, m_valueValueLabel));

    // Reset saturation/value to the automatic bus tint values.
    auto* resetBtn = new QPushButton("Reset saturation/value to auto", this);
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        m_s = vvvdaw::AutoStripSaturation;
        m_v = vvvdaw::AutoStripValue;
        m_saturationSlider->setValue(m_s);
        m_valueSlider->setValue(m_v);
    });
    root->addWidget(resetBtn, 0, Qt::AlignLeft);

    // Recent colours.
    root->addWidget(new QLabel("Recent", this));
    m_recentLayout = new QHBoxLayout;
    m_recentLayout->setSpacing(4);
    root->addLayout(m_recentLayout);

    // OK / Cancel.
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* okBtn = new QPushButton("OK", this);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, [this] {
        recordRecent(m_current);
        accept();
    });
    footer->addWidget(cancelBtn);
    footer->addWidget(okBtn);
    root->addLayout(footer);

    connect(m_hueSlider, &QSlider::valueChanged, this, [this](int v) { m_h = v; updatePreview(); });
    connect(m_saturationSlider, &QSlider::valueChanged, this,
            [this](int v) { m_s = v; updatePreview(); });
    connect(m_valueSlider, &QSlider::valueChanged, this, [this](int v) { m_v = v; updatePreview(); });

    loadRecent();
    rebuildRecent();

    // Match the rest of the UI's dark theme.
    setStyleSheet(
        "QDialog { background-color: #252525; color: #dddddd; }"
        "QSlider::groove:horizontal { height: 4px; background: #3a3a3a; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: #88ccff; "
        "  border-radius: 6px; }"
        "QPushButton { background: #2f2f2f; color: #dddddd; border: 1px solid #555; "
        "  border-radius: 4px; padding: 4px 10px; }"
        "QPushButton:hover { border-color: #88ccff; }"
        "QLabel { color: #dddddd; }");

    applyInitial(initial);
}

QHBoxLayout* BusColorPaletteDialog::makeSliderRow(const QString& text, QSlider*& slider,
                                                  int min, int max, QLabel*& valueLabel) {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* label = new QLabel(text, this);
    label->setMinimumWidth(24);
    row->addWidget(label);
    slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->addWidget(slider, 1);
    valueLabel = new QLabel(this);
    valueLabel->setMinimumWidth(34);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(valueLabel);
    return row;
}

QColor BusColorPaletteDialog::currentColor() const {
    return QColor::fromHsv(m_h, m_s, m_v);
}

void BusColorPaletteDialog::applyInitial(const QColor& color) {
    const QColor start = color.isValid() ? color : suggestedColors().value(0, Qt::black);
    const int hue = start.hsvHue();
    m_h = (hue < 0) ? 0 : hue;
    // Saturation/value start at the automatic bus tint values, not at the
    // (possibly desaturated/very dark) value of the current strip color.
    m_s = vvvdaw::AutoStripSaturation;
    m_v = vvvdaw::AutoStripValue;
    // Update each slider once (signals blocked) and refresh the preview.
    m_hueSlider->blockSignals(true);
    m_saturationSlider->blockSignals(true);
    m_valueSlider->blockSignals(true);
    m_hueSlider->setValue(m_h);
    m_saturationSlider->setValue(m_s);
    m_valueSlider->setValue(m_v);
    m_hueSlider->blockSignals(false);
    m_saturationSlider->blockSignals(false);
    m_valueSlider->blockSignals(false);
    updatePreview();
}

void BusColorPaletteDialog::updatePreview() {
    const QColor c = currentColor();
    m_current = c;
    m_preview->setStyleSheet(QString("background-color: %1; border: 1px solid #555; "
                                     "border-radius: 4px;").arg(c.name()));
    m_hexLabel->setText(c.name());
    m_hueValueLabel->setText(QString::number(m_h));
    m_saturationValueLabel->setText(QString::number(m_s));
    m_valueValueLabel->setText(QString::number(m_v));
}

void BusColorPaletteDialog::loadRecent() {
    m_recent = suggestedColors();
    const QStringList hexValues = QSettings().value("bus/colorRecent").toStringList();
    for (const QString& hex : hexValues) {
        const QColor c(hex);
        if (c.isValid())
            m_recent.append(c);
    }
    while (m_recent.size() > kRecentCount)
        m_recent.removeLast();
}

void BusColorPaletteDialog::saveRecent() {
    QStringList hexValues;
    for (const QColor& c : m_recent)
        hexValues << c.name();
    QSettings().setValue("bus/colorRecent", hexValues);
}

void BusColorPaletteDialog::recordRecent(const QColor& color) {
    m_recent.removeAll(color);
    m_recent.prepend(color);
    while (m_recent.size() > kRecentCount)
        m_recent.removeLast();
    saveRecent();
    rebuildRecent();
}

void BusColorPaletteDialog::rebuildRecent() {
    while (QLayoutItem* item = m_recentLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            m_recentLayout->removeWidget(w);
            delete w;
        }
        delete item;
    }
    for (const QColor& c : m_recent) {
        auto* swatch = new QToolButton(this);
        swatch->setFixedSize(22, 22);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setStyleSheet(QString(
            "QToolButton { background-color: %1; border: 1px solid #555; "
            "border-radius: 3px; } QToolButton:hover { border: 1px solid #ffffff; }")
            .arg(c.name()));
        swatch->setToolTip(c.name());
        connect(swatch, &QToolButton::clicked, this, [this, c] {
            m_current = c;
            recordRecent(c);
            accept();
        });
        m_recentLayout->addWidget(swatch);
    }
    m_recentLayout->addStretch(1);
}
