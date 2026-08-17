#include "BusPanelWidget.h"
#include "BusLevelMeter.h"
#include "PluginListWidget.h"
#include "BusSendsWidget.h"
#include "audio/AudioEngine.h"
#include "audio/AudioUtils.h"
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
#include <QMouseEvent>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QInputDialog>
#include <cmath>

namespace {

const char* const kBusDragMime = "application/x-vvvdaw-bus-drag";

// Map a linear volume to the dB-scaled fader position (value 0..100 <-> -60..0 dB).
int volumeToSlider(float linearVolume) {
    float db = linearToDecibels(linearVolume);
    return static_cast<int>(std::lround((db + 60.0f) * 100.0f / 60.0f));
}

// Map a dB-scaled fader position (value 0..100 <-> -60..0 dB) to linear volume.
float sliderToVolume(int value) {
    float db = -60.0f + 60.0f * value / 100.0f;
    return decibelsToLinear(db);
}

} // namespace

BusPanelWidget::BusPanelWidget(Project& project, QWidget* parent)
    : QScrollArea(parent)
    , m_project(project)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(true);

    m_container = new QWidget(this);
    m_container->setObjectName("busContainer");
    m_container->setAutoFillBackground(true);
    m_container->setAcceptDrops(true);
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

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(30);
    connect(m_meterTimer, &QTimer::timeout, this, &BusPanelWidget::updateMeters);
    m_meterTimer->start();
}

void BusPanelWidget::rebuild() {
    std::vector<bool> panelOpenBefore(m_busRows.size(), false);
    for (size_t i = 0; i < m_busRows.size(); ++i)
        if (m_busRows[i].panelToggle && m_busRows[i].panelToggle->isChecked())
            panelOpenBefore[i] = true;

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
    m_busRows.assign(buses.size(), BusRow());

    m_panelOpen.assign(buses.size(), false);
    for (size_t i = 0; i < panelOpenBefore.size() && i < m_panelOpen.size(); ++i)
        m_panelOpen[i] = panelOpenBefore[i];

    // Drop selection entries that no longer exist.
    m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(),
                                    [&buses](int b) {
                                        return b < 0 || b >= static_cast<int>(buses.size());
                                    }),
                     m_selected.end());
    if (m_selectionAnchor < 0 || m_selectionAnchor >= static_cast<int>(buses.size()))
        m_selectionAnchor = -1;

    m_renderOrder.clear();
    renderBusTree(0, 0); // master is the root
    for (int idx : m_project.topLevelBusIndices())
        renderBusTree(idx, 0);

    m_containerLayout->addStretch();
    updateSelectionStyles();
}

void BusPanelWidget::renderBusTree(int busIndex, int depth) {
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busRows.size()))
        return;
    if (depth > 0) {
        auto* spacer = new QWidget(m_container);
        spacer->setFixedWidth(depth * 14);
        m_containerLayout->addWidget(spacer);
    }
    buildBusStrip(busIndex);
    m_renderOrder.push_back(busIndex);
    if (m_project.isBusFolder(busIndex) && !m_project.buses()[busIndex].folderCollapsed()) {
        for (int c : m_project.folderChildren(busIndex))
            renderBusTree(c, depth + 1);
    }
}

void BusPanelWidget::buildBusStrip(int busIndex) {
    const auto& buses = m_project.buses();
    const auto& bus = buses[busIndex];
    BusRow row;

    auto btnStyle = [](const QString& normal, const QString& checked) {
        return normal + "; padding: 0px; }"
             + checked + "; padding: 0px; }";
    };

    QColor stripColor = (busIndex % 2 == 0) ? QColor("#2e2e2e") : QColor("#333333");

    row.widget = new QWidget(m_container);
    row.widget->setObjectName("busStrip");
    // Fixed horizontal policy: the strip must keep its sizeHint width
    // (narrow controls) no matter how wide the panel gets, and only widen
    // when the plugin panel is actually revealed (sizeHint then grows).
    row.widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    row.widget->setAutoFillBackground(true);
    QPalette wp = row.widget->palette();
    wp.setColor(QPalette::Window, stripColor);
    row.widget->setPalette(wp);
    row.widget->installEventFilter(this);

    auto* stripLayout = new QHBoxLayout(row.widget);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(0);

    // --- Controls column (fixed width) ---
    row.controls = new QWidget(row.widget);
    row.controls->setFixedWidth(kControlsWidth);
    row.controls->setAutoFillBackground(true);
    QPalette cp = row.controls->palette();
    cp.setColor(QPalette::Window, stripColor);
    row.controls->setPalette(cp);
    stripLayout->addWidget(row.controls);
    row.controls->installEventFilter(this);

    auto* layout = new QVBoxLayout(row.controls);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(1);

    row.nameEdit = new QLineEdit(bus.name(), row.controls);
    row.nameEdit->setReadOnly(true);
    row.nameEdit->setAlignment(Qt::AlignCenter);
    row.nameEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 9px; color: #ccc; }"
        "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
    );
    layout->addWidget(row.nameEdit);

    auto* smRow = new QHBoxLayout;
    smRow->setSpacing(2);
    smRow->addStretch(1);

    row.soloButton = new QPushButton("S", row.controls);
    row.soloButton->setObjectName("soloButton");
    row.soloButton->setFixedSize(16, 16);
    row.soloButton->setCheckable(true);
    row.soloButton->setChecked(bus.isSolo());
    row.soloButton->setStyleSheet(
        btnStyle(
            "QPushButton { background: #443322; color: #ccaa66; border: 1px solid #665544; font-weight: bold; font-size: 8px",
            "QPushButton:checked { background: #cc8800; color: white; border: 2px solid #ffaa00; font-weight: bold; font-size: 8px"
        )
    );
    row.soloButton->setToolTip("Solo");
    smRow->addWidget(row.soloButton);

    row.muteButton = new QPushButton("M", row.controls);
    row.muteButton->setObjectName("muteButton");
    row.muteButton->setFixedSize(16, 16);
    row.muteButton->setCheckable(true);
    row.muteButton->setChecked(bus.isMuted());
    row.muteButton->setStyleSheet(
        btnStyle(
            "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 8px",
            "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; font-weight: bold; font-size: 8px"
        )
    );
    row.muteButton->setToolTip("Mute");
    smRow->addWidget(row.muteButton);

    // Fold/unfold control for folder buses (a bus with children routed in).
    if (m_project.isBusFolder(busIndex)) {
        row.folderToggle = new QPushButton(bus.folderCollapsed() ? "\u25B8" : "\u25BE", row.controls);
        row.folderToggle->setObjectName("folderToggle");
        row.folderToggle->setFixedSize(16, 16);
        row.folderToggle->setCheckable(true);
        row.folderToggle->setChecked(!bus.folderCollapsed());
        row.folderToggle->setStyleSheet(
            "QPushButton { background: #333333; color: #88aacc; border: 1px solid #445566; font-weight: bold; font-size: 9px; padding: 0px; }"
            "QPushButton:checked { background: #224466; color: white; border: 1px solid #4488cc; font-weight: bold; font-size: 9px; padding: 0px; }"
        );
        row.folderToggle->setToolTip("Fold/unfold folder contents");
        smRow->addWidget(row.folderToggle);
        connect(row.folderToggle, &QPushButton::toggled, this, [this, busIndex, row](bool checked) {
            bool oldVal = m_project.buses()[busIndex].folderCollapsed();
            bool newVal = !checked;
            emit busFolderCollapseWillChange(busIndex, oldVal, newVal);
            m_project.buses()[busIndex].setFolderCollapsed(newVal);
            row.folderToggle->setText(checked ? "\u25BE" : "\u25B8");
            rebuild();
        });
    }

    smRow->addStretch(1);
    layout->addLayout(smRow);

    row.panSlider = new QSlider(Qt::Horizontal, row.controls);
    row.panSlider->setRange(-100, 100);
    row.panSlider->setValue(static_cast<int>(bus.pan() * 100));
    row.panSlider->setFixedHeight(10);
    row.panSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 2px; border-radius: 1px; }"
        "QSlider::handle:horizontal { background: #aaa; width: 5px; margin: -3px 0; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #6688cc; border-radius: 1px; }"
    );
    layout->addWidget(row.panSlider);

    auto* meterRow = new QHBoxLayout;
    meterRow->setSpacing(2);
    meterRow->setContentsMargins(0, 0, 0, 0);

    row.levelMeter = new BusLevelMeter(row.controls);
    row.levelMeter->setObjectName("levelMeter");
    row.levelMeter->setFixedWidth(30);
    row.levelMeter->setVolume(bus.volume());
    meterRow->addWidget(row.levelMeter, 1);
    row.volumeSlider = new QSlider(Qt::Vertical, row.controls);
    row.volumeSlider->setObjectName("volumeSlider");
    row.volumeSlider->setRange(0, 100);
    // The slider shares the meter's dB scale: value 0..100 <-> -60..0 dB.
    row.volumeSlider->setValue(volumeToSlider(bus.volume()));
    row.volumeSlider->setFixedWidth(26);
    row.volumeSlider->setTickPosition(QSlider::TicksLeft);
    row.volumeSlider->setTickInterval(20); // 20 units = 12 dB
    row.volumeSlider->setStyleSheet(
        "QSlider::groove:vertical { background: #444; width: 3px; border-radius: 1px; }"
        "QSlider::handle:vertical { background: #aaa; height: 5px; margin: 0 -2px; border-radius: 2px; }"
        "QSlider::add-page:vertical { background: #44aa44; border-radius: 1px; }"
    );
    meterRow->addWidget(row.volumeSlider);

    layout->addLayout(meterRow, 1);

    auto* outRow = new QHBoxLayout;
    row.outCombo = new QComboBox(row.controls);
    row.outCombo->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 9px; padding: 1px 2px; }"
        "QComboBox::drop-down { border: none; width: 12px; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
    );
    row.outCombo->addItem("Output Device", -1);
    for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
        if (j == busIndex) continue;
        bool cycle = wouldCreateBusCycle(buses, busIndex, j);
        row.outCombo->addItem(buses[j].name(), j);
        int lastIdx = row.outCombo->count() - 1;
        if (cycle) {
            row.outCombo->setItemData(lastIdx, QVariant(), Qt::UserRole - 1);
            row.outCombo->setItemText(lastIdx, buses[j].name() + " (x)");
        }
    }
    int outTarget = bus.outputBusIndex();
    for (int c = 0; c < row.outCombo->count(); ++c) {
        if (row.outCombo->itemData(c).toInt() == outTarget) {
            row.outCombo->setCurrentIndex(c);
            break;
        }
    }
    outRow->addWidget(row.outCombo, 1);
    row.panelToggle = new QPushButton("Fx", row.controls);
    row.panelToggle->setObjectName("panelToggle");
    row.panelToggle->setCheckable(true);
    row.panelToggle->setFixedSize(20, 20);
    row.panelToggle->setStyleSheet(
        btnStyle(
            "QPushButton { background: #333344; color: #88aacc; border: 1px solid #445566; font-weight: bold; font-size: 8px",
            "QPushButton:checked { background: #224466; color: white; border: 1px solid #4488cc; font-weight: bold; font-size: 8px"
        )
    );
    row.panelToggle->setToolTip("Show/hide plugins and sends");
    outRow->addWidget(row.panelToggle);
    layout->addLayout(outRow);

    // --- Combined plugins + sends panel (hidden until "Fx" is toggled) ---
    // Plugins on top, sends below, sharing the strip's height.
    row.fxPanel = new QWidget(row.widget);
    row.fxPanel->setObjectName("busFxPanel");
    row.fxPanel->setFixedWidth(kPluginPanelWidth);
    auto* fxLayout = new QVBoxLayout(row.fxPanel);
    fxLayout->setContentsMargins(0, 0, 0, 0);
    fxLayout->setSpacing(2);

    row.pluginList = new PluginListWidget(row.fxPanel);
    row.pluginList->setObjectName("busPluginList");
    row.pluginList->setHeaderLabel("Effects:");
    row.pluginList->setBus(const_cast<AudioBus*>(&bus));
    row.pluginList->setPluginManager(m_pluginManager);
    row.pluginList->setAudioParams(m_sampleRate, m_bufferSize);
    row.pluginList->rebuild();
    fxLayout->addWidget(row.pluginList, 1);

    row.sendsList = new BusSendsWidget(row.fxPanel);
    row.sendsList->setObjectName("busSendList");
    row.sendsList->setHeaderLabel("Sends:");
    row.sendsList->setProject(&m_project, busIndex);
    row.sendsList->rebuild();
    fxLayout->addWidget(row.sendsList, 1);

    stripLayout->addWidget(row.fxPanel);

    const bool panelOpen = (busIndex >= 0 && busIndex < static_cast<int>(m_panelOpen.size()))
        && m_panelOpen[busIndex];
    row.fxPanel->setVisible(panelOpen);

    connect(row.panelToggle, &QPushButton::toggled, this, [this, busIndex, row](bool checked) {
        row.fxPanel->setVisible(checked);
        if (busIndex >= 0 && busIndex < static_cast<int>(m_panelOpen.size()))
            m_panelOpen[busIndex] = checked;
    });
    row.panelToggle->setChecked(panelOpen);

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

    connect(row.sendsList, &BusSendsWidget::sendAddRequested, this,
            [this, busIndex](int) {
        emit busSendAddRequested(busIndex);
        rebuild();
    });
    connect(row.sendsList, &BusSendsWidget::sendRemoveRequested, this,
            [this, busIndex](int, int sendIndex) {
        emit busSendRemoveRequested(busIndex, sendIndex);
        rebuild();
    });
    connect(row.sendsList, &BusSendsWidget::sendTargetWillChange, this,
            [this, busIndex](int, int sendIndex, int oldBus, int newBus) {
        emit busSendTargetWillChange(busIndex, sendIndex, oldBus, newBus);
    });
    connect(row.sendsList, &BusSendsWidget::sendLevelWillChange, this,
            [this, busIndex](int, int sendIndex, float oldLevel, float newLevel) {
        emit busSendLevelWillChange(busIndex, sendIndex, oldLevel, newLevel);
    });
    connect(row.sendsList, &BusSendsWidget::sendPreWillChange, this,
            [this, busIndex](int, int sendIndex, bool oldPre, bool newPre) {
        emit busSendPreWillChange(busIndex, sendIndex, oldPre, newPre);
    });

    connect(row.soloButton, &QPushButton::toggled, this, [this, busIndex](bool checked) {
        bool oldVal = m_project.buses()[busIndex].isSolo();
        emit busSoloWillChange(busIndex, oldVal, checked);
        m_project.buses()[busIndex].setSolo(checked);
        emit busChanged();
    });
    connect(row.muteButton, &QPushButton::toggled, this, [this, busIndex](bool checked) {
        bool oldVal = m_project.buses()[busIndex].isMuted();
        emit busMuteWillChange(busIndex, oldVal, checked);
        m_project.buses()[busIndex].setMuted(checked);
        emit busChanged();
    });

    connect(row.nameEdit, &QLineEdit::editingFinished, this, [this, row, busIndex] {
        QString text = row.nameEdit->text().trimmed();
        if (!text.isEmpty() && text != m_project.buses()[busIndex].name()) {
            QString oldName = m_project.buses()[busIndex].name();
            emit busNameWillChange(busIndex, oldName, text);
            m_project.buses()[busIndex].setName(text);
            emit busChanged();
        }
        row.nameEdit->setReadOnly(true);
    });

    connect(row.outCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, row, busIndex](int comboIdx) {
        int targetBusIdx = row.outCombo->itemData(comboIdx).toInt();
        if (targetBusIdx >= 0 && wouldCreateBusCycle(m_project.buses(), busIndex, targetBusIdx)) {
            int outTarget = m_project.buses()[busIndex].outputBusIndex();
            for (int c = 0; c < row.outCombo->count(); ++c) {
                if (row.outCombo->itemData(c).toInt() == outTarget) {
                    row.outCombo->setCurrentIndex(c);
                    break;
                }
            }
            return;
        }
        int oldVal = m_project.buses()[busIndex].outputBusIndex();
        emit busOutputWillChange(busIndex, oldVal, targetBusIdx);
        m_project.buses()[busIndex].setOutputBusIndex(targetBusIdx);
        emit busChanged();
    });

    connect(row.panSlider, &QSlider::valueChanged, this,
            [this, busIndex](int val) {
        float oldVal = m_project.buses()[busIndex].pan();
        float newVal = val / 100.0f;
        emit busPanWillChange(busIndex, oldVal, newVal);
        m_project.buses()[busIndex].setPan(newVal);
        emit busChanged();
    });

    connect(row.volumeSlider, &QSlider::valueChanged, this,
            [this, row, busIndex](int val) {
        float oldVal = m_project.buses()[busIndex].volume();
        float newVal = sliderToVolume(val);
        emit busVolumeWillChange(busIndex, oldVal, newVal);
        m_project.buses()[busIndex].setVolume(newVal);
        row.levelMeter->setVolume(newVal);
        emit busChanged();
    });

    m_containerLayout->addWidget(row.widget);
    m_busRows[busIndex] = row;
}

void BusPanelWidget::updateMeters() {
    if (!m_engine) return;
    for (int i = 0; i < static_cast<int>(m_busRows.size()); ++i) {
        auto& row = m_busRows[i];
        if (!row.levelMeter) continue;
        float peak = m_engine->busMeterPeak(i);
        bool clipped = m_engine->busMeterClipping(i);
        row.levelMeter->setPeak(peak);
        row.levelMeter->setClipping(clipped);
        if (clipped)
            m_engine->clearBusMeterClip(i);
    }
}

void BusPanelWidget::refreshOutCombos() {
    const auto& buses = m_project.buses();
    for (int from = 0; from < static_cast<int>(m_busRows.size()); ++from) {
        QComboBox* combo = m_busRows[from].outCombo;
        if (!combo) continue;
        for (int c = 0; c < combo->count(); ++c) {
            int busIdx = combo->itemData(c).toInt();
            if (busIdx < 0 || busIdx >= static_cast<int>(buses.size())) continue;
            bool cycle = wouldCreateBusCycle(buses, from, busIdx);
            combo->setItemText(c, cycle ? buses[busIdx].name() + " (x)" : buses[busIdx].name());
        }
        if (m_busRows[from].sendsList)
            m_busRows[from].sendsList->refreshTargetCombos();
    }
}

bool BusPanelWidget::isSelected(int busIndex) const {
    return std::find(m_selected.begin(), m_selected.end(), busIndex) != m_selected.end();
}

void BusPanelWidget::setSelected(const std::vector<int>& buses) {
    m_selected = buses;
    updateSelectionStyles();
}

void BusPanelWidget::toggleSelected(int busIndex) {
    auto it = std::find(m_selected.begin(), m_selected.end(), busIndex);
    if (it != m_selected.end())
        m_selected.erase(it);
    else
        m_selected.push_back(busIndex);
    updateSelectionStyles();
}

void BusPanelWidget::handleStripClick(int busIndex, Qt::KeyboardModifiers modifiers) {
    if (modifiers & Qt::ControlModifier) {
        toggleSelected(busIndex);
        m_selectionAnchor = busIndex;
    } else if (modifiers & Qt::ShiftModifier) {
        if (m_selectionAnchor < 0 || m_renderOrder.empty()) {
            setSelected({ busIndex });
            m_selectionAnchor = busIndex;
            return;
        }
        auto itA = std::find(m_renderOrder.begin(), m_renderOrder.end(), m_selectionAnchor);
        auto itB = std::find(m_renderOrder.begin(), m_renderOrder.end(), busIndex);
        if (itA == m_renderOrder.end() || itB == m_renderOrder.end()) {
            setSelected({ busIndex });
            m_selectionAnchor = busIndex;
            return;
        }
        std::vector<int> range;
        auto lo = std::min(itA, itB);
        auto hi = std::max(itA, itB);
        for (auto it = lo; it != hi; ++it)
            range.push_back(*it);
        range.push_back(*hi);
        setSelected(range);
    } else {
        setSelected({ busIndex });
        m_selectionAnchor = busIndex;
    }
}

void BusPanelWidget::updateSelectionStyles() {
    for (int i = 0; i < static_cast<int>(m_busRows.size()); ++i) {
        QWidget* controls = m_busRows[i].controls;
        if (!controls) continue;
        QColor base = (i % 2 == 0) ? QColor("#2e2e2e") : QColor("#333333");
        QColor bg = isSelected(i) ? base.lighter(140) : base;
        QPalette pal = controls->palette();
        pal.setColor(QPalette::Window, bg);
        controls->setPalette(pal);
    }
}

int BusPanelWidget::busIndexForWidget(QWidget* widget) const {
    QWidget* w = widget;
    while (w) {
        for (int i = 0; i < static_cast<int>(m_busRows.size()); ++i) {
            if (m_busRows[i].widget == w)
                return i;
        }
        w = w->parentWidget();
    }
    return -1;
}

int BusPanelWidget::busIndexAt(const QPoint& pos) const {
    for (int i = 0; i < static_cast<int>(m_busRows.size()); ++i) {
        if (m_busRows[i].widget && m_busRows[i].widget->geometry().contains(pos))
            return i;
    }
    return -1;
}

// Remove `dragged` from `order` and re-insert them (preserving their relative
// order) right before `beforeIndex`. An absent/negative `beforeIndex` appends
// them at the end.
static std::vector<int> moveOrderElements(const std::vector<int>& order,
                                          const std::vector<int>& dragged,
                                          int beforeIndex) {
    std::vector<int> draggedList;
    for (int o : order) {
        if (std::find(dragged.begin(), dragged.end(), o) != dragged.end())
            draggedList.push_back(o);
    }
    std::vector<int> out;
    out.reserve(order.size());
    for (int o : order) {
        if (std::find(dragged.begin(), dragged.end(), o) == dragged.end())
            out.push_back(o);
    }
    auto it = std::find(out.begin(), out.end(), beforeIndex);
    size_t insertPos = (it == out.end()) ? out.size() : static_cast<size_t>(it - out.begin());
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(insertPos),
               draggedList.begin(), draggedList.end());
    return out;
}

void BusPanelWidget::startBusDrag(int busIndex) {
    if (busIndex < 0) return;
    std::vector<int> dragged = m_selected;
    if (dragged.empty() || !isSelected(busIndex))
        dragged = { busIndex };

    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    QByteArray data;
    for (int b : dragged) {
        data += QByteArray::number(b);
        data += ';';
    }
    mime->setData(kBusDragMime, data);
    drag->setMimeData(mime);
    if (busIndex >= 0 && busIndex < static_cast<int>(m_busRows.size()) && m_busRows[busIndex].widget)
        drag->setPixmap(m_busRows[busIndex].widget->grab());
    drag->exec(Qt::MoveAction);
}

void BusPanelWidget::handleBusDrop(const QPoint& pos, const std::vector<int>& dragged) {
    if (dragged.empty()) return;

    int target = busIndexAt(pos);
    std::vector<int> oldOrder = m_project.busDisplayOrder();
    std::vector<int> newOrder = oldOrder;
    std::vector<std::pair<int, int>> oldParents;
    std::vector<std::pair<int, int>> newParents;
    for (int b : dragged) {
        if (const AudioBus* bus = m_project.busAt(b))
            oldParents.emplace_back(b, bus->outputBusIndex());
    }

    if (target > 0 && m_project.isBusFolder(target)) {
        // Drop on a folder: move the dragged buses into it (cycle-safe).
        for (int b : dragged) {
            if (b == target || wouldCreateBusCycle(m_project.buses(), b, target))
                continue;
            newParents.emplace_back(b, target);
        }
        auto orderIt = std::find(oldOrder.begin(), oldOrder.end(), target);
        int beforeIndex = -1;
        if (orderIt != oldOrder.end() && orderIt + 1 != oldOrder.end())
            beforeIndex = *(orderIt + 1);
        newOrder = moveOrderElements(oldOrder, dragged, beforeIndex);
    } else if (target > 0) {
        // Drop on a regular strip: reorder the dragged buses before it.
        newOrder = moveOrderElements(oldOrder, dragged, target);
    } else {
        // Drop on empty space: move the dragged buses out to the master level.
        for (int b : dragged)
            if (b != 0)
                newParents.emplace_back(b, 0);
        newOrder = moveOrderElements(oldOrder, dragged, -1);
    }

    emit busesMoved(oldOrder, newOrder, oldParents, newParents);
    rebuild();
}

void BusPanelWidget::moveBusesToFolder(const std::vector<int>& targets, int folder) {
    if (targets.empty()) return;
    std::vector<int> order = m_project.busDisplayOrder();
    std::vector<std::pair<int, int>> oldParents;
    std::vector<std::pair<int, int>> newParents;
    for (int t : targets) {
        if (const AudioBus* bus = m_project.busAt(t))
            oldParents.emplace_back(t, bus->outputBusIndex());
        if (t != folder && !wouldCreateBusCycle(m_project.buses(), t, folder))
            newParents.emplace_back(t, folder);
    }
    emit busesMoved(order, order, oldParents, newParents);
    rebuild();
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

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            int idx = busIndexForWidget(qobject_cast<QWidget*>(obj));
            if (idx >= 0) {
                handleStripClick(idx, me->modifiers());
                m_dragSource = idx;
                m_dragStartPos = me->globalPosition().toPoint();
                // Accept so the press does not propagate up to the strip /
                // container filters (which would toggle selection twice).
                return true;
            }
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
            m_dragSource = -1;
        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (m_dragSource >= 0 && (me->buttons() & Qt::LeftButton) &&
            (me->globalPosition().toPoint() - m_dragStartPos).manhattanLength() >
                QApplication::startDragDistance()) {
            int src = m_dragSource;
            m_dragSource = -1;
            startBusDrag(src);
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        int idx = busIndexForWidget(qobject_cast<QWidget*>(obj));
        if (idx >= 0) {
            const auto& buses = m_project.buses();
            QMenu menu(m_busRows[idx].widget);

            std::vector<int> targets = m_selected;
            if (targets.empty() || !isSelected(idx))
                targets = { idx };

            // "Put to folder…" (folders are buses; membership = main output).
            if (std::find(targets.begin(), targets.end(), 0) == targets.end()) {
                QMenu* folderMenu = menu.addMenu("Put to folder\u2026");
                for (int f = 1; f < static_cast<int>(buses.size()); ++f) {
                    if (std::find(targets.begin(), targets.end(), f) != targets.end())
                        continue;
                    bool ok = true;
                    for (int t : targets) {
                        if (t == f || wouldCreateBusCycle(buses, t, f)) { ok = false; break; }
                    }
                    if (!ok) continue;
                    QString label = buses[f].name();
                    if (m_project.isBusFolder(f))
                        label = "\u25B8 " + label;
                    QAction* a = folderMenu->addAction(label);
                    connect(a, &QAction::triggered, this, [this, targets, f] {
                        moveBusesToFolder(targets, f);
                    });
                }
                QAction* newFolder = folderMenu->addAction("New folder\u2026");
                connect(newFolder, &QAction::triggered, this, [this, targets] {
                    bool ok = false;
                    QString name = QInputDialog::getText(this, "New folder",
                                                         "Folder name:", QLineEdit::Normal,
                                                         QString("Folder"), &ok);
                    if (!ok || name.trimmed().isEmpty()) return;
                    emit createBusFolderRequested(name.trimmed(), targets);
                });
            }

            if (idx > 0 && buses[idx].removable()) {
                menu.addSeparator();
                QAction* deleteAction = menu.addAction("Delete Bus");
                connect(deleteAction, &QAction::triggered, this, [this, idx] {
                    emit removeBusRequested(idx);
                });
            }
            menu.exec(ce->globalPos());
            return true;
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

    if (obj == m_container && event->type() == QEvent::DragEnter) {
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (de->mimeData()->hasFormat(kBusDragMime)) {
            de->acceptProposedAction();
            return true;
        }
    }
    if (obj == m_container && event->type() == QEvent::DragMove) {
        auto* dm = static_cast<QDragMoveEvent*>(event);
        if (dm->mimeData()->hasFormat(kBusDragMime)) {
            dm->acceptProposedAction();
            return true;
        }
    }
    if (obj == m_container && event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (de->mimeData()->hasFormat(kBusDragMime)) {
            std::vector<int> dragged;
            for (const auto& part : de->mimeData()->data(kBusDragMime).split(';')) {
                bool ok = false;
                int v = part.toInt(&ok);
                if (ok && v >= 0)
                    dragged.push_back(v);
            }
            handleBusDrop(de->position().toPoint(), dragged);
            de->acceptProposedAction();
            return true;
        }
    }

    return QScrollArea::eventFilter(obj, event);
}
