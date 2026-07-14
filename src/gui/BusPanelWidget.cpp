#include "BusPanelWidget.h"
#include "PluginListWidget.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QEvent>
#include <QMenu>
#include <QDialog>
#include <QListWidget>
#include <QContextMenuEvent>
#include <QPushButton>

static bool wouldCreateCycle(const std::vector<AudioBus>& buses, int fromIndex, int toIndex) {
    if (toIndex < 0) return false;
    if (toIndex == fromIndex) return true;
    int busCount = static_cast<int>(buses.size());
    int current = toIndex;
    for (int step = 0; step < busCount; ++step) {
        if (current == fromIndex) return true;
        if (current < 0 || current >= busCount) return false;
        current = buses[current].outputBusIndex;
    }
    return false;
}

BusPanelWidget::BusPanelWidget(Project& project, QWidget* parent)
    : QScrollArea(parent)
    , m_project(project)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(true);

    m_container = new QWidget(this);
    m_container->setAutoFillBackground(true);
    QPalette pal = m_container->palette();
    pal.setColor(QPalette::Window, QColor("#252525"));
    m_container->setPalette(pal);
    m_container->installEventFilter(this);

    auto* rootLayout = new QHBoxLayout(m_container);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    m_containerLayout = new QHBoxLayout;
    m_containerLayout->setContentsMargins(0, 0, 0, 0);
    m_containerLayout->setSpacing(4);
    rootLayout->addLayout(m_containerLayout, 1);

    setWidget(m_container);
}

void BusPanelWidget::rebuild() {
    for (auto& row : m_busRows) {
        if (row.widget) {
            row.widget->hide();
            row.widget->deleteLater();
        }
    }
    m_busRows.clear();

    while (auto* item = m_containerLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const auto& buses = m_project.buses();

    for (int i = 0; i < static_cast<int>(buses.size()); ++i) {
        const auto& bus = buses[i];
        BusRow row;

        row.widget = new QWidget(m_container);
        row.widget->setFixedWidth(200);
        row.widget->setAutoFillBackground(true);
        QPalette wp = row.widget->palette();
        wp.setColor(QPalette::Window, (i % 2 == 0) ? QColor("#2e2e2e") : QColor("#333333"));
        row.widget->setPalette(wp);
        row.widget->installEventFilter(this);

        auto* layout = new QVBoxLayout(row.widget);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);

        auto* topRow = new QHBoxLayout;
        row.nameEdit = new QLineEdit(bus.name, row.widget);
        row.nameEdit->setReadOnly(true);
        row.nameEdit->setStyleSheet(
            "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 11px; color: #ccc; }"
            "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
        );
        topRow->addWidget(row.nameEdit, 1);

        auto btnStyle = [](const QString& normal, const QString& checked) {
            return normal + "; padding: 0px; }"
                 + checked + "; padding: 0px; }";
        };

        row.soloButton = new QPushButton("S", row.widget);
        row.soloButton->setFixedSize(22, 22);
        row.soloButton->setCheckable(true);
        row.soloButton->setChecked(bus.solo);
        row.soloButton->setStyleSheet(
            btnStyle(
                "QPushButton { background: #443322; color: #ccaa66; border: 1px solid #665544; font-weight: bold; font-size: 10px",
                "QPushButton:checked { background: #cc8800; color: white; border: 2px solid #ffaa00; font-weight: bold; font-size: 10px"
            )
        );
        row.soloButton->setToolTip("Solo");
        topRow->addWidget(row.soloButton);

        row.muteButton = new QPushButton("M", row.widget);
        row.muteButton->setFixedSize(22, 22);
        row.muteButton->setCheckable(true);
        row.muteButton->setChecked(bus.muted);
        row.muteButton->setStyleSheet(
            btnStyle(
                "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 10px",
                "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; font-weight: bold; font-size: 10px"
            )
        );
        row.muteButton->setToolTip("Mute");
        topRow->addWidget(row.muteButton);

        layout->addLayout(topRow);

        auto* panRow = new QHBoxLayout;
        auto* panLabel = new QLabel("pan:", row.widget);
        panLabel->setStyleSheet("font-size: 9px; color: #999;");
        panRow->addWidget(panLabel);
        row.panSlider = new QSlider(Qt::Horizontal, row.widget);
        row.panSlider->setRange(-100, 100);
        row.panSlider->setValue(static_cast<int>(bus.pan * 100));
        row.panSlider->setFixedHeight(12);
        row.panSlider->setStyleSheet(
            "QSlider::groove:horizontal { background: #444; height: 3px; border-radius: 1px; }"
            "QSlider::handle:horizontal { background: #aaa; width: 8px; margin: -3px 0; border-radius: 4px; }"
            "QSlider::sub-page:horizontal { background: #6688cc; border-radius: 1px; }"
        );
        panRow->addWidget(row.panSlider, 1);
        layout->addLayout(panRow);

        auto* volRow = new QHBoxLayout;
        auto* volLabel = new QLabel("level:", row.widget);
        volLabel->setStyleSheet("font-size: 9px; color: #999;");
        volRow->addWidget(volLabel);
        row.volumeSlider = new QSlider(Qt::Horizontal, row.widget);
        row.volumeSlider->setRange(0, 100);
        row.volumeSlider->setValue(static_cast<int>(bus.volume * 100));
        row.volumeSlider->setFixedHeight(12);
        row.volumeSlider->setStyleSheet(
            "QSlider::groove:horizontal { background: #444; height: 3px; border-radius: 1px; }"
            "QSlider::handle:horizontal { background: #aaa; width: 8px; margin: -3px 0; border-radius: 4px; }"
            "QSlider::sub-page:horizontal { background: #44aa44; border-radius: 1px; }"
        );
        volRow->addWidget(row.volumeSlider, 1);
        layout->addLayout(volRow);

        auto* outRow = new QHBoxLayout;
        auto* outLabel = new QLabel("out:", row.widget);
        outLabel->setStyleSheet("font-size: 9px; color: #999;");
        outRow->addWidget(outLabel);
        row.outCombo = new QComboBox(row.widget);
        row.outCombo->setStyleSheet(
            "QComboBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 9px; padding: 1px 3px; }"
            "QComboBox::drop-down { border: none; width: 12px; }"
            "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
        );
        row.outCombo->addItem("Output Device", -1);
        for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
            if (j == i) continue;
            bool cycle = wouldCreateCycle(buses, i, j);
            row.outCombo->addItem(buses[j].name, j);
            int lastIdx = row.outCombo->count() - 1;
            if (cycle) {
                row.outCombo->setItemData(lastIdx, QVariant(), Qt::UserRole - 1);
                row.outCombo->setItemText(lastIdx, buses[j].name + " (x)");
            }
        }
        int outTarget = bus.outputBusIndex;
        for (int c = 0; c < row.outCombo->count(); ++c) {
            if (row.outCombo->itemData(c).toInt() == outTarget) {
                row.outCombo->setCurrentIndex(c);
                break;
            }
        }
        outRow->addWidget(row.outCombo, 1);
        layout->addLayout(outRow);

        int busIndex = i;

        row.pluginList = new PluginListWidget(row.widget);
        row.pluginList->setBus(const_cast<AudioBus*>(&bus));
        row.pluginList->setPluginManager(m_pluginManager);
        row.pluginList->setAudioParams(m_sampleRate, m_bufferSize);
        row.pluginList->rebuild();
        connect(row.pluginList, &PluginListWidget::openEditorRequested, this,
                [this, busIndex](PluginInstance* plugin) {
            emit openBusPluginEditorRequested(busIndex, plugin);
        });
        connect(row.pluginList, &PluginListWidget::pluginWillBeRemoved, this,
                [this](PluginInstance* plugin) { emit busPluginWillBeRemoved(plugin); });
        connect(row.pluginList, &PluginListWidget::pluginAddRequested, this,
                [this, busIndex](const QString& type, const QString& path) {
            emit busPluginAddRequested(busIndex, type, path);
        });
        connect(row.pluginList, &PluginListWidget::pluginRemoved, this,
                [this, busIndex](int idx) { emit busPluginRemoved(busIndex, idx); });
        connect(row.pluginList, &PluginListWidget::pluginWillBeMoved, this,
                [this, busIndex](int from, int to) { emit busPluginWillBeMoved(busIndex, from, to); });
        connect(row.pluginList, &PluginListWidget::pluginWillBeToggled, this,
                [this, busIndex]() { emit busPluginWillBeToggled(busIndex); });
        layout->addWidget(row.pluginList, 1);

        connect(row.soloButton, &QPushButton::toggled, this, [this, busIndex](bool checked) {
            bool oldVal = m_project.buses()[busIndex].solo;
            emit busSoloWillChange(busIndex, oldVal, checked);
            m_project.buses()[busIndex].solo = checked;
            emit busChanged();
        });
        connect(row.muteButton, &QPushButton::toggled, this, [this, busIndex](bool checked) {
            bool oldVal = m_project.buses()[busIndex].muted;
            emit busMuteWillChange(busIndex, oldVal, checked);
            m_project.buses()[busIndex].muted = checked;
            emit busChanged();
        });

        connect(row.nameEdit, &QLineEdit::editingFinished, this, [this, row, busIndex] {
            QString text = row.nameEdit->text().trimmed();
            if (!text.isEmpty() && text != m_project.buses()[busIndex].name) {
                QString oldName = m_project.buses()[busIndex].name;
                emit busNameWillChange(busIndex, oldName, text);
                m_project.buses()[busIndex].name = text;
                emit busChanged();
            }
            row.nameEdit->setReadOnly(true);
        });

        row.nameEdit->installEventFilter(this);

        connect(row.outCombo, QOverload<int>::of(&QComboBox::activated), this,
                [this, row, busIndex](int comboIdx) {
            int targetBusIdx = row.outCombo->itemData(comboIdx).toInt();
            if (targetBusIdx >= 0 && wouldCreateCycle(m_project.buses(), busIndex, targetBusIdx)) {
                int outTarget = m_project.buses()[busIndex].outputBusIndex;
                for (int c = 0; c < row.outCombo->count(); ++c) {
                    if (row.outCombo->itemData(c).toInt() == outTarget) {
                        row.outCombo->setCurrentIndex(c);
                        break;
                    }
                }
                return;
            }
            int oldVal = m_project.buses()[busIndex].outputBusIndex;
            emit busOutputWillChange(busIndex, oldVal, targetBusIdx);
            m_project.buses()[busIndex].outputBusIndex = targetBusIdx;
            emit busChanged();
        });

        connect(row.panSlider, &QSlider::valueChanged, this,
                [this, busIndex](int val) {
            float oldVal = m_project.buses()[busIndex].pan;
            float newVal = val / 100.0f;
            emit busPanWillChange(busIndex, oldVal, newVal);
            m_project.buses()[busIndex].pan = newVal;
            emit busChanged();
        });

        connect(row.volumeSlider, &QSlider::valueChanged, this,
                [this, busIndex](int val) {
            float oldVal = m_project.buses()[busIndex].volume;
            float newVal = val / 100.0f;
            emit busVolumeWillChange(busIndex, oldVal, newVal);
            m_project.buses()[busIndex].volume = newVal;
            emit busChanged();
        });

        m_containerLayout->addWidget(row.widget);
        m_busRows.push_back(row);
    }

    m_containerLayout->addStretch();
}

bool BusPanelWidget::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (auto& row : m_busRows) {
            if (row.nameEdit == obj) {
                row.nameEdit->setReadOnly(false);
                row.nameEdit->selectAll();
                row.nameEdit->setFocus();
                return true;
            }
        }
    }
    if (event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        for (int i = 0; i < static_cast<int>(m_busRows.size()); ++i) {
            if (m_busRows[i].widget == obj) {
                if (!m_project.buses()[i].removable) return true;
                QMenu menu(m_busRows[i].widget);
                QAction* deleteAction = menu.addAction("Delete Bus");
                connect(deleteAction, &QAction::triggered, this, [this, i] {
                    emit removeBusRequested(i);
                });
                menu.exec(ce->globalPos());
                return true;
            }
        }
        if (obj == m_container) {
            QMenu menu(m_container);
            QAction* addAction = menu.addAction("Add Bus");
            connect(addAction, &QAction::triggered, this, [this] {
                emit addBusRequested();
            });
            menu.exec(ce->globalPos());
            return true;
        }
    }
    return QScrollArea::eventFilter(obj, event);
}
