#include "SidechainDialog.h"
#include "audio/AudioUtils.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include "plugin/PluginInstance.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStandardItemModel>
#include <QVBoxLayout>

SidechainDialog::SidechainDialog(Project& project, int ownerBusIndex,
                                 PluginInstance* plugin, QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_ownerBusIndex(ownerBusIndex)
    , m_plugin(plugin) {
    setWindowTitle(QString("Sidechain source \u2014 %1")
                       .arg(plugin ? plugin->name() : QString()));
    setMinimumWidth(320);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        "Pick the bus whose signal feeds this plugin's sidechain input. The "
        "source bus receives a send at the chosen level.", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* busRow = new QHBoxLayout;
    busRow->addWidget(new QLabel("Source bus:", this));
    m_busCombo = new QComboBox(this);
    m_busCombo->setObjectName("sidechainBusCombo");
    m_busCombo->addItem("None", -1);
    auto* busModel = qobject_cast<QStandardItemModel*>(m_busCombo->model());
    const auto& buses = m_project.buses();
    for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
        if (j == ownerBusIndex)
            continue;
        bool cycle = wouldCreateBusCycle(buses, j, ownerBusIndex);
        QString label = buses[j].name();
        if (cycle)
            label += " (x)";
        m_busCombo->addItem(label, j);
        if (cycle) {
            int itemIdx = m_busCombo->count() - 1;
            m_busCombo->setItemData(itemIdx, QVariant(), Qt::UserRole - 1);
            // The Qt::UserRole - 1 marker only hides the item in some styles;
            // disable it explicitly so it cannot be selected at all.
            if (busModel)
                if (QStandardItem* item = busModel->item(itemIdx))
                    item->setEnabled(false);
        }
    }
    busRow->addWidget(m_busCombo, 1);
    layout->addLayout(busRow);

    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(new QLabel("Send level:", this));
    m_levelSlider = new QSlider(Qt::Horizontal, this);
    m_levelSlider->setObjectName("sidechainLevelSlider");
    m_levelSlider->setRange(0, 100);
    levelRow->addWidget(m_levelSlider, 1);
    layout->addLayout(levelRow);

    m_preToggle = new QPushButton("Post", this);
    m_preToggle->setObjectName("sidechainPreToggle");
    m_preToggle->setCheckable(true);
    m_preToggle->setToolTip("Tap the source bus before (Pre) or after (Post) its fader");
    layout->addWidget(m_preToggle);

    // Seed the widgets from the current assignment, if any.
    SidechainSendLocation loc =
        findSidechainSend(m_project.buses(), ownerBusIndex,
                          plugin ? plugin->pluginId() : QString());
    float level = 1.0f;
    bool pre = false;
    int selected = -1;
    if (loc.sourceBus >= 0) {
        selected = loc.sourceBus;
        const auto& sends = buses[static_cast<size_t>(loc.sourceBus)].sends();
        if (loc.sendIndex >= 0 && loc.sendIndex < static_cast<int>(sends.size())) {
            level = sends[static_cast<size_t>(loc.sendIndex)].level;
            pre = sends[static_cast<size_t>(loc.sendIndex)].preFader;
        }
    }
    int comboIdx = m_busCombo->findData(selected);
    m_busCombo->setCurrentIndex(comboIdx >= 0 ? comboIdx : 0);
    m_levelSlider->setValue(volumeToSliderPos(level));
    m_preToggle->setChecked(pre);
    m_preToggle->setText(pre ? "Pre" : "Post");
    connect(m_preToggle, &QPushButton::toggled, this, [this](bool checked) {
        m_preToggle->setText(checked ? "Pre" : "Post");
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int SidechainDialog::sourceBus() const {
    return m_busCombo->currentData().toInt();
}

float SidechainDialog::level() const {
    return sliderPosToVolume(m_levelSlider->value());
}

bool SidechainDialog::preFader() const {
    return m_preToggle->isChecked();
}
