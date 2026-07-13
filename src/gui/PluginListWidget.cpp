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
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QApplication>

static const char* const kMimePluginIndex = "application/x-vvvdaw-plugin-index";

PluginListWidget::PluginListWidget(QWidget* parent)
    : QWidget(parent) {
    setAcceptDrops(true);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(2);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setMinimumHeight(0);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_container = new QWidget();
    m_container->setAcceptDrops(true);
    m_containerLayout = new QVBoxLayout(m_container);
    m_containerLayout->setContentsMargins(2, 2, 2, 2);
    m_containerLayout->setSpacing(1);

    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea, 1);

    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedWidth(28);
    m_addButton->setFixedHeight(20);
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
    row->setAcceptDrops(false);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(4);

    auto* enableBtn = new QPushButton(plugin->isEnabled() ? "ON" : "OFF", row);
    enableBtn->setFixedWidth(32);
    enableBtn->setCheckable(true);
    enableBtn->setChecked(plugin->isEnabled());
    connect(enableBtn, &QPushButton::toggled, this, [this, plugin, enableBtn](bool checked) {
        emit pluginWillBeToggled();
        plugin->setEnabled(checked);
        enableBtn->setText(checked ? "ON" : "OFF");
    });
    layout->addWidget(enableBtn);

    auto* nameLabel = new QLabel(plugin->name(), row);
    nameLabel->setStyleSheet("color: #ddd; font-size: 10px;");
    layout->addWidget(nameLabel, 1);

    auto* removeBtn = new QPushButton("x", row);
    removeBtn->setFixedWidth(20);
    removeBtn->setFixedHeight(18);
    removeBtn->setStyleSheet(
        "QPushButton { background: #443333; color: #cc8888; border: 1px solid #554444; font-size: 10px; font-weight: bold; }"
        "QPushButton:hover { background: #663333; color: #ff8888; }"
    );
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
        onRemoveClicked(index);
    });
    layout->addWidget(removeBtn);

    row->setStyleSheet("background: #333; border-radius: 3px; padding: 1px;");
    row->setCursor(Qt::OpenHandCursor);

    row->installEventFilter(this);

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
            instance->activate(m_sampleRate, m_bufferSize);
            int idx = chain->count();
            emit pluginAdded(idx);
            chain->addPlugin(std::move(instance));
            rebuild();
        }
    }
}

void PluginListWidget::onRemoveClicked(int index) {
    auto* chain = targetChain();
    if (!chain) return;
    auto* plugin = chain->plugin(index);
    if (plugin) emit pluginWillBeRemoved(plugin);
    emit pluginRemoved(index);
    chain->removePlugin(index);
    rebuild();
}

bool PluginListWidget::eventFilter(QObject* obj, QEvent* event) {
    auto* w = qobject_cast<QWidget*>(obj);
    if (!w) return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            int idx = rowAtPos(w->pos());
            if (idx >= 0) {
                auto* chain = targetChain();
                if (chain && idx < chain->count()) {
                    auto* plugin = chain->plugin(idx);
                    if (plugin) emit openEditorRequested(plugin);
                }
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            m_dragFromIndex = rowAtPos(w->pos());
            m_dragStartPos = me->pos();
            w->setCursor(Qt::ClosedHandCursor);
            return false;
        }
    }

    if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (m_dragFromIndex >= 0 && (me->pos() - m_dragStartPos).manhattanLength() > QApplication::startDragDistance()) {
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData();
            mime->setData(kMimePluginIndex, QByteArray::number(m_dragFromIndex));
            drag->setMimeData(mime);

            int idx = m_dragFromIndex;
            if (idx >= 0 && idx < static_cast<int>(m_rows.size())) {
                QPixmap pixmap(m_rows[idx]->size());
                m_rows[idx]->render(&pixmap);
                drag->setPixmap(pixmap);
            }

            w->setCursor(Qt::OpenHandCursor);
            drag->exec(Qt::MoveAction);
            m_dragFromIndex = -1;
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            m_dragFromIndex = -1;
            w->setCursor(Qt::OpenHandCursor);
            return false;
        }
    }

    return QWidget::eventFilter(obj, event);
}

int PluginListWidget::rowAtPos(const QPoint& pos) const {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[i]->geometry().contains(pos) || m_rows[i]->pos() == pos) {
            return i;
        }
    }
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (pos.y() >= m_rows[i]->pos().y() && pos.y() < m_rows[i]->pos().y() + m_rows[i]->height()) {
            return i;
        }
    }
    return -1;
}

void PluginListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kMimePluginIndex))
        event->acceptProposedAction();
}

void PluginListWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat(kMimePluginIndex)) {
        event->acceptProposedAction();
    }
}

void PluginListWidget::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(kMimePluginIndex)) return;

    int fromIndex = event->mimeData()->data(kMimePluginIndex).toInt();
    int toIndex = rowAtPos(event->position().toPoint());

    auto* chain = targetChain();
    if (!chain || fromIndex < 0 || fromIndex >= chain->count()) return;
    if (toIndex < 0) toIndex = chain->count() - 1;
    if (toIndex >= chain->count()) toIndex = chain->count() - 1;
    if (fromIndex == toIndex) return;

    emit pluginWillBeMoved(fromIndex, toIndex);
    chain->movePlugin(fromIndex, toIndex);
    rebuild();
    event->acceptProposedAction();
}
