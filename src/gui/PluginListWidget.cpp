#include "PluginListWidget.h"
#include "PluginWindow.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include "plugin/VST3Instance.h"
#include "plugin/LV2Instance.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QDialog>
#include <QVBoxLayout>
#include <QListWidget>
#include <QMessageBox>

PluginListWidget::PluginListWidget(QWidget* parent)
    : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 4, 0, 4);
    m_mainLayout->setSpacing(2);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setFixedHeight(120);

    m_container = new QWidget();
    m_containerLayout = new QVBoxLayout(m_container);
    m_containerLayout->setContentsMargins(4, 2, 4, 2);
    m_containerLayout->setSpacing(2);
    m_containerLayout->addStretch();

    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea);

    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedWidth(28);
    m_addButton->setFixedHeight(22);
    m_addButton->setToolTip("Add Plugin");
    connect(m_addButton, &QPushButton::clicked, this, &PluginListWidget::onAddClicked);
    m_mainLayout->addWidget(m_addButton);
}

void PluginListWidget::setTrack(Track* track) {
    m_track = track;
    m_bus = nullptr;
}

void PluginListWidget::setBus(AudioBus* bus) {
    m_bus = bus;
    m_track = nullptr;
}

PluginChain* PluginListWidget::targetChain() const {
    if (m_track) return const_cast<PluginChain*>(&m_track->pluginChain());
    if (m_bus) return &m_bus->pluginChain;
    return nullptr;
}

void PluginListWidget::rebuild() {
    for (auto* row : m_rows) {
        m_containerLayout->removeWidget(row);
        row->deleteLater();
    }
    m_rows.clear();

    auto* chain = targetChain();
    if (!chain) return;

    for (int i = 0; i < chain->count(); ++i)
        buildRow(chain->plugin(i), i);
}

void PluginListWidget::buildRow(PluginInstance* plugin, int index) {
    auto* row = new QWidget();
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(4);

    auto* nameLabel = new QLabel(plugin->name(), row);
    nameLabel->setStyleSheet("color: #ddd; font-size: 11px;");
    layout->addWidget(nameLabel, 1);

    auto* enableBtn = new QPushButton(plugin->isEnabled() ? "ON" : "OFF", row);
    enableBtn->setFixedWidth(32);
    enableBtn->setCheckable(true);
    enableBtn->setChecked(plugin->isEnabled());
    connect(enableBtn, &QPushButton::toggled, [plugin](bool checked) {
        plugin->setEnabled(checked);
    });
    layout->addWidget(enableBtn);

    if (plugin->hasEditor()) {
        auto* editorBtn = new QPushButton("GUI", row);
        editorBtn->setFixedWidth(32);
        connect(editorBtn, &QPushButton::clicked, this, [this, index]() {
            onEditorClicked(index);
        });
        layout->addWidget(editorBtn);
    }

    auto* upBtn = new QPushButton("^", row);
    upBtn->setFixedWidth(22);
    connect(upBtn, &QPushButton::clicked, this, [this, index]() {
        onMoveUpClicked(index);
    });
    layout->addWidget(upBtn);

    auto* downBtn = new QPushButton("v", row);
    downBtn->setFixedWidth(22);
    connect(downBtn, &QPushButton::clicked, this, [this, index]() {
        onMoveDownClicked(index);
    });
    layout->addWidget(downBtn);

    auto* removeBtn = new QPushButton("x", row);
    removeBtn->setFixedWidth(22);
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
        onRemoveClicked(index);
    });
    layout->addWidget(removeBtn);

    row->setStyleSheet("background: #333; border-radius: 3px; padding: 1px;");
    m_containerLayout->insertWidget(m_containerLayout->count() - 1, row);
    m_rows.push_back(row);
}

void PluginListWidget::onAddClicked() {
    auto* chain = targetChain();
    if (!chain || !m_pluginManager) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Add Plugin");
    dialog.setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(&dialog);
    auto* listWidget = new QListWidget(&dialog);

    for (const auto& pi : m_pluginManager->plugins()) {
        auto* item = new QListWidgetItem(QString("[%1] %2").arg(pi.type.toUpper(), pi.name));
        item->setData(Qt::UserRole, pi.pluginId);
        item->setData(Qt::UserRole + 1, pi.type);
        item->setData(Qt::UserRole + 2, pi.path);
        listWidget->addItem(item);
    }

    layout->addWidget(listWidget);

    auto* buttons = new QHBoxLayout();
    auto* okBtn = new QPushButton("Add", &dialog);
    auto* cancelBtn = new QPushButton("Cancel", &dialog);
    buttons->addWidget(okBtn);
    buttons->addWidget(cancelBtn);
    layout->addLayout(buttons);

    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && listWidget->currentItem()) {
        auto* item = listWidget->currentItem();
        QString type = item->data(Qt::UserRole + 1).toString();
        QString id = item->data(Qt::UserRole).toString();
        QString path = item->data(Qt::UserRole + 2).toString();

        std::unique_ptr<PluginInstance> instance;
        if (type == "vst3") {
            auto vst3 = std::make_unique<VST3Instance>();
            if (vst3->load(path)) instance = std::move(vst3);
        } else if (type == "lv2") {
            const LilvPlugin* lilvPlugin = m_pluginManager->findLV2Plugin(id);
            if (lilvPlugin) {
                auto lv2 = std::make_unique<LV2Instance>(m_pluginManager->lilvWorld(), lilvPlugin);
                instance = std::move(lv2);
            }
        }

        if (instance) {
            int idx = chain->count();
            chain->addPlugin(std::move(instance));
            rebuild();
            emit pluginAdded(idx);
        }
    }
}

void PluginListWidget::onRemoveClicked(int index) {
    auto* chain = targetChain();
    if (!chain) return;
    chain->removePlugin(index);
    rebuild();
    emit pluginRemoved(index);
}

void PluginListWidget::onEditorClicked(int index) {
    auto* chain = targetChain();
    if (!chain) return;
    auto* plugin = chain->plugin(index);
    if (plugin) emit openEditorRequested(plugin);
}

void PluginListWidget::onMoveUpClicked(int index) {
    auto* chain = targetChain();
    if (!chain || index <= 0) return;
    chain->movePlugin(index, index - 1);
    rebuild();
    emit pluginMoved(index, index - 1);
}

void PluginListWidget::onMoveDownClicked(int index) {
    auto* chain = targetChain();
    if (!chain) return;
    if (index >= chain->count() - 1) return;
    chain->movePlugin(index, index + 1);
    rebuild();
    emit pluginMoved(index, index + 1);
}
