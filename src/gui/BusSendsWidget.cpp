#include "BusSendsWidget.h"
#include "audio/AudioUtils.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QMenu>
#include <QEvent>
#include <QSpacerItem>
#include <QContextMenuEvent>
#include <cmath>

BusSendsWidget::BusSendsWidget(QWidget* parent)
    : QWidget(parent) {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(2);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setMinimumHeight(0);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_container = new QWidget();
    m_containerLayout = new QVBoxLayout(m_container);
    m_containerLayout->setContentsMargins(2, 2, 2, 2);
    m_containerLayout->setSpacing(1);

    m_scrollArea->setWidget(m_container);

    // Header row: optional caption on the left, the "+" add button on the
    // right, above the list.
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);
    m_headerLabel = new QLabel(this);
    m_headerLabel->setStyleSheet("color: #889; font-size: 9px;");
    m_headerLabel->hide();
    header->addWidget(m_headerLabel, 1);

    m_addButton = new QPushButton("+", this);
    m_addButton->setObjectName("sendAddButton");
    m_addButton->setFixedWidth(28);
    m_addButton->setFixedHeight(20);
    m_addButton->setToolTip("Add Send");
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        if (m_busIndex >= 0)
            emit sendAddRequested(m_busIndex);
    });
    header->addWidget(m_addButton);
    m_mainLayout->addLayout(header);

    m_mainLayout->addWidget(m_scrollArea, 1);
}

void BusSendsWidget::setHeaderLabel(const QString& text) {
    m_headerLabel->setText(text);
    m_headerLabel->setVisible(!text.isEmpty());
}

AudioBus* BusSendsWidget::currentBus() const {
    if (!m_project || m_busIndex < 0)
        return nullptr;
    return m_project->busAt(m_busIndex);
}

void BusSendsWidget::rebuild() {
    for (auto& row : m_rows) {
        m_containerLayout->removeWidget(row.widget);
        row.widget->deleteLater();
    }
    m_rows.clear();

    if (m_trailingStretch) {
        m_containerLayout->removeItem(m_trailingStretch);
        delete m_trailingStretch;
        m_trailingStretch = nullptr;
    }

    auto* bus = currentBus();
    if (!bus) return;

    int count = static_cast<int>(bus->sends().size());
    for (int i = 0; i < count; ++i)
        buildRow(bus->sends()[static_cast<size_t>(i)], i);

    m_trailingStretch = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_containerLayout->addItem(m_trailingStretch);
}

void BusSendsWidget::refreshTargetCombos() {
    if (!m_project) return;
    const auto& buses = m_project->buses();
    for (auto& row : m_rows) {
        if (!row.combo) continue;
        int current = row.combo->currentData().toInt();
        row.combo->blockSignals(true);
        row.combo->clear();
        for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
            if (j == m_busIndex) continue;
            bool cycle = wouldCreateBusCycle(buses, m_busIndex, j);
            row.combo->addItem(cycle ? buses[j].name() + " (x)" : buses[j].name(), j);
            int lastIdx = row.combo->count() - 1;
            if (cycle)
                row.combo->setItemData(lastIdx, QVariant(), Qt::UserRole - 1);
        }
        int idx = row.combo->findData(current);
        row.combo->setCurrentIndex(idx >= 0 ? idx : 0);
        row.combo->blockSignals(false);
    }
}

void BusSendsWidget::buildRow(AudioBus::Send& send, int index) {
    const auto& buses = m_project->buses();

    Row row;
    row.widget = new QWidget();
    row.widget->setObjectName("sendRow");
    row.widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row.widget->setMinimumHeight(22);
    row.widget->setStyleSheet("background: #333; border-radius: 3px; padding: 1px;");
    row.widget->installEventFilter(this);

    auto* layout = new QHBoxLayout(row.widget);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(4);

    row.combo = new QComboBox(row.widget);
    row.combo->setObjectName("sendTargetCombo");
    row.combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row.combo->setStyleSheet(
        "QComboBox { background: #444; color: #ccc; border: 1px solid #666; font-size: 9px; padding: 1px 2px; }"
        "QComboBox::drop-down { border: none; width: 12px; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
    );
    for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
        if (j == m_busIndex) continue;
        bool cycle = wouldCreateBusCycle(buses, m_busIndex, j);
        row.combo->addItem(cycle ? buses[j].name() + " (x)" : buses[j].name(), j);
        int lastIdx = row.combo->count() - 1;
        if (cycle)
            row.combo->setItemData(lastIdx, QVariant(), Qt::UserRole - 1);
    }
    int curIdx = row.combo->findData(send.busIndex);
    row.combo->setCurrentIndex(curIdx >= 0 ? curIdx : 0);
    layout->addWidget(row.combo, 1);

    row.level = new QSlider(Qt::Horizontal, row.widget);
    row.level->setObjectName("sendLevelSlider");
    row.level->setRange(0, 100);
    row.level->setValue(volumeToSliderPos(send.level));
    row.level->setFixedWidth(64);
    row.level->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 2px; border-radius: 1px; }"
        "QSlider::handle:horizontal { background: #aaa; width: 5px; margin: -3px 0; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #6688cc; border-radius: 1px; }"
    );
    row.level->setToolTip("Send level");
    layout->addWidget(row.level);

    row.preToggle = new QPushButton(send.preFader ? "Pre" : "Post", row.widget);
    row.preToggle->setObjectName("sendPreToggle");
    row.preToggle->setFixedWidth(32);
    row.preToggle->setCheckable(true);
    row.preToggle->setChecked(send.preFader);
    row.preToggle->setToolTip("Tap pre- or post-fader");
    row.preToggle->setStyleSheet(
        "QPushButton { background: #334; color: #8ac; border: 1px solid #456; font-size: 8px; padding: 0px; }"
        "QPushButton:checked { background: #6688cc; color: white; border: 1px solid #aac; font-size: 8px; padding: 0px; }"
    );
    layout->addWidget(row.preToggle);

    m_containerLayout->addWidget(row.widget);
    m_rows.push_back(row);

    connect(row.combo, QOverload<int>::of(&QComboBox::activated), this,
            [this, index, row](int comboIdx) {
        int target = row.combo->itemData(comboIdx).toInt();
        if (target >= 0 && wouldCreateBusCycle(m_project->buses(), m_busIndex, target)) {
            const auto& sends = currentBus()->sends();
            int oldTarget = (index >= 0 && index < static_cast<int>(sends.size()))
                                ? sends[static_cast<size_t>(index)].busIndex : -1;
            for (int c = 0; c < row.combo->count(); ++c) {
                if (row.combo->itemData(c).toInt() == oldTarget) {
                    row.combo->setCurrentIndex(c);
                    break;
                }
            }
            return;
        }
        auto* bus = currentBus();
        if (!bus || index < 0 || index >= static_cast<int>(bus->sends().size())) return;
        int oldTarget = bus->sends()[static_cast<size_t>(index)].busIndex;
        if (oldTarget == target) return;
        emit sendTargetWillChange(m_busIndex, index, oldTarget, target);
        bus->sends()[static_cast<size_t>(index)].setBus(target);
        refreshTargetCombos();
    });

    connect(row.level, &QSlider::valueChanged, this, [this, index](int val) {
        auto* bus = currentBus();
        if (!bus || index < 0 || index >= static_cast<int>(bus->sends().size())) return;
        float oldLevel = bus->sends()[static_cast<size_t>(index)].level;
        float newLevel = sliderPosToVolume(val);
        if (std::fabs(oldLevel - newLevel) < 1e-5f) return;
        emit sendLevelWillChange(m_busIndex, index, oldLevel, newLevel);
        bus->sends()[static_cast<size_t>(index)].setLevel(newLevel);
    });

    connect(row.preToggle, &QPushButton::toggled, this, [this, row, index](bool checked) {
        auto* bus = currentBus();
        if (!bus || index < 0 || index >= static_cast<int>(bus->sends().size())) return;
        bool oldPre = bus->sends()[static_cast<size_t>(index)].preFader;
        if (oldPre == checked) return;
        emit sendPreWillChange(m_busIndex, index, oldPre, checked);
        bus->sends()[static_cast<size_t>(index)].setPreFader(checked);
        row.preToggle->setText(checked ? "Pre" : "Post");
    });
}

int BusSendsWidget::rowAtPos(const QPoint& pos) const {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[i].widget->geometry().contains(pos) || m_rows[i].widget->pos() == pos)
            return i;
    }
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (pos.y() >= m_rows[i].widget->pos().y() &&
            pos.y() < m_rows[i].widget->pos().y() + m_rows[i].widget->height()) {
            return i;
        }
    }
    return -1;
}

bool BusSendsWidget::eventFilter(QObject* obj, QEvent* event) {
    auto* w = qobject_cast<QWidget*>(obj);
    if (!w) return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        int idx = rowAtPos(w->pos());
        auto* bus = currentBus();
        if (idx >= 0 && bus && idx < static_cast<int>(bus->sends().size())) {
            QMenu menu(w);
            QAction* removeAction = menu.addAction("Remove Send");
            connect(removeAction, &QAction::triggered, this, [this, idx] {
                emit sendRemoveRequested(m_busIndex, idx);
            });
            menu.exec(ce->globalPos());
        }
        return true;
    }

    return QWidget::eventFilter(obj, event);
}
