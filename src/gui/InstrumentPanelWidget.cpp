#include "InstrumentPanelWidget.h"
#include "PluginListWidget.h"
#include "model/Project.h"
#include "model/Instrument.h"
#include "model/AudioBus.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QEvent>
#include <QMenu>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QContextMenuEvent>

static bool instrumentCycleCheck(const std::vector<AudioBus>& buses, int toIndex) {
    if (toIndex < 0) return false;
    int busCount = static_cast<int>(buses.size());
    int current = toIndex;
    for (int step = 0; step < busCount + 1; ++step) {
        if (current < 0 || current >= busCount) return false;
        current = buses[current].outputBusIndex;
    }
    return true;
}

InstrumentPanelWidget::InstrumentPanelWidget(Project& project, QWidget* parent)
    : QScrollArea(parent)
    , m_project(project)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(true);

    m_container = new QWidget(this);
    m_container->setAutoFillBackground(true);
    QPalette pal = m_container->palette();
    pal.setColor(QPalette::Window, QColor("#222222"));
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

void InstrumentPanelWidget::rebuild() {
    for (auto& row : m_instrumentRows) {
        if (row.widget) {
            row.widget->hide();
            row.widget->deleteLater();
        }
    }
    m_instrumentRows.clear();

    while (auto* item = m_containerLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const auto& instruments = m_project.instruments();
    const auto& buses = m_project.buses();

    auto btnStyle = [](const QString& normal, const QString& checked) {
        return normal + "; padding: 0px; }"
             + checked + "; padding: 0px; }";
    };

    for (int i = 0; i < static_cast<int>(instruments.size()); ++i) {
        const auto& instrument = instruments[i];
        InstrumentRow row;

        row.widget = new QWidget(m_container);
        row.widget->setFixedWidth(220);
        row.widget->setAutoFillBackground(true);
        QPalette wp = row.widget->palette();
        wp.setColor(QPalette::Window, (i % 2 == 0) ? QColor("#2b2b2b") : QColor("#303030"));
        row.widget->setPalette(wp);
        row.widget->installEventFilter(this);

        auto* layout = new QVBoxLayout(row.widget);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);

        auto* topRow = new QHBoxLayout;
        row.nameEdit = new QLineEdit(instrument.name(), row.widget);
        row.nameEdit->setReadOnly(true);
        row.nameEdit->setStyleSheet(
            "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 11px; color: #eee; }"
            "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
        );
        topRow->addWidget(row.nameEdit, 1);

        row.soloButton = new QPushButton("S", row.widget);
        row.soloButton->setFixedSize(22, 22);
        row.soloButton->setCheckable(true);
        row.soloButton->setChecked(instrument.isSolo());
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
        row.muteButton->setChecked(instrument.isMuted());
        row.muteButton->setStyleSheet(
            btnStyle(
                "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 10px",
                "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; font-weight: bold; font-size: 10px"
            )
        );
        row.muteButton->setToolTip("Mute");
        topRow->addWidget(row.muteButton);

        layout->addLayout(topRow);

        auto* synthRow = new QHBoxLayout;
        auto* synthLabel = new QLabel("synth:", row.widget);
        synthLabel->setStyleSheet("font-size: 9px; color: #999;");
        synthRow->addWidget(synthLabel);
        row.synthButton = new QPushButton(
            instrument.synth() ? instrument.synth()->name() : "No Synth", row.widget);
        row.synthButton->setStyleSheet(
            "QPushButton { background: #333; color: #8cf; border: 1px solid #445; font-size: 10px; padding: 2px 4px; text-align: left; }"
            "QPushButton:hover { border-color: #66aaff; }"
        );
        row.synthButton->setCursor(Qt::PointingHandCursor);
        synthRow->addWidget(row.synthButton, 1);
        row.synthRemoveButton = new QPushButton("x", row.widget);
        row.synthRemoveButton->setFixedSize(20, 20);
        row.synthRemoveButton->setToolTip("Remove Synth");
        row.synthRemoveButton->setVisible(instrument.synth() != nullptr);
        row.synthRemoveButton->setStyleSheet(
            "QPushButton { background: #443333; color: #cc8888; border: 1px solid #554444; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background: #663333; color: #ff8888; }"
        );
        synthRow->addWidget(row.synthRemoveButton);
        layout->addLayout(synthRow);

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
        row.outCombo->addItem("Master", 0);
        for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
            if (j == 0) continue;
            row.outCombo->addItem(buses[j].name, j);
        }
        int outTarget = instrument.outputBusIndex();
        for (int c = 0; c < row.outCombo->count(); ++c) {
            if (row.outCombo->itemData(c).toInt() == outTarget) {
                row.outCombo->setCurrentIndex(c);
                break;
            }
        }
        outRow->addWidget(row.outCombo, 1);
        layout->addLayout(outRow);

        auto* panRow = new QHBoxLayout;
        auto* panLabel = new QLabel("pan:", row.widget);
        panLabel->setStyleSheet("font-size: 9px; color: #999;");
        panRow->addWidget(panLabel);
        row.panSlider = new QSlider(Qt::Horizontal, row.widget);
        row.panSlider->setRange(-100, 100);
        row.panSlider->setValue(static_cast<int>(instrument.pan() * 100));
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
        row.volumeSlider->setValue(static_cast<int>(instrument.volume() * 100));
        row.volumeSlider->setFixedHeight(12);
        row.volumeSlider->setStyleSheet(
            "QSlider::groove:horizontal { background: #444; height: 3px; border-radius: 1px; }"
            "QSlider::handle:horizontal { background: #aaa; width: 8px; margin: -3px 0; border-radius: 4px; }"
            "QSlider::sub-page:horizontal { background: #44aa44; border-radius: 1px; }"
        );
        volRow->addWidget(row.volumeSlider, 1);
        layout->addLayout(volRow);

        int instIndex = i;

        row.pluginList = new PluginListWidget(row.widget);
        row.pluginList->setInstrument(const_cast<Instrument*>(&instrument));
        row.pluginList->setPluginManager(m_pluginManager);
        row.pluginList->setAudioParams(m_sampleRate, m_bufferSize);
        row.pluginList->rebuild();
        connect(row.pluginList, &PluginListWidget::openEditorRequested, this,
                [this, instIndex](PluginInstance* plugin) {
            emit openFxEditorRequested(instIndex, plugin);
        });
        connect(row.pluginList, &PluginListWidget::pluginWillBeRemoved, this,
                [this](PluginInstance* plugin) { emit pluginWillBeRemoved(plugin); });
        connect(row.pluginList, &PluginListWidget::pluginAddRequested, this,
                [this, instIndex](const QString& type, const QString& path) {
            emit fxAddRequested(instIndex, type, path);
        });
        connect(row.pluginList, &PluginListWidget::pluginRemoved, this,
                [this, instIndex](int idx) { emit pluginRemoved(instIndex, idx); });
        connect(row.pluginList, &PluginListWidget::pluginWillBeMoved, this,
                [this, instIndex](int from, int to) { emit pluginWillBeMoved(instIndex, from, to); });
        connect(row.pluginList, &PluginListWidget::pluginWillBeToggled, this,
                [this, instIndex]() { emit pluginWillBeToggled(instIndex); });
        layout->addWidget(row.pluginList, 1);

        connect(row.soloButton, &QPushButton::toggled, this, [this, instIndex](bool checked) {
            bool oldVal = m_project.instruments()[instIndex].isSolo();
            emit soloWillChange(instIndex, oldVal, checked);
            m_project.instruments()[instIndex].setSolo(checked);
            emit instrumentChanged();
        });
        connect(row.muteButton, &QPushButton::toggled, this, [this, instIndex](bool checked) {
            bool oldVal = m_project.instruments()[instIndex].isMuted();
            emit muteWillChange(instIndex, oldVal, checked);
            m_project.instruments()[instIndex].setMuted(checked);
            emit instrumentChanged();
        });

        connect(row.synthButton, &QPushButton::clicked, this, [this, instIndex] {
            openSynthDialog(instIndex);
        });
        connect(row.synthRemoveButton, &QPushButton::clicked, this, [this, instIndex] {
            emit synthRemoveRequested(instIndex);
        });

        connect(row.nameEdit, &QLineEdit::editingFinished, this, [this, row, instIndex] {
            QString text = row.nameEdit->text().trimmed();
            if (!text.isEmpty() && text != m_project.instruments()[instIndex].name()) {
                QString oldName = m_project.instruments()[instIndex].name();
                emit nameWillChange(instIndex, oldName, text);
                m_project.instruments()[instIndex].setName(text);
                emit instrumentChanged();
            }
            row.nameEdit->setReadOnly(true);
        });
        row.nameEdit->installEventFilter(this);

        connect(row.outCombo, QOverload<int>::of(&QComboBox::activated), this,
                [this, row, instIndex](int comboIdx) {
            int targetBusIdx = row.outCombo->itemData(comboIdx).toInt();
            if (instrumentCycleCheck(m_project.buses(), targetBusIdx)) {
                int outTarget = m_project.instruments()[instIndex].outputBusIndex();
                for (int c = 0; c < row.outCombo->count(); ++c) {
                    if (row.outCombo->itemData(c).toInt() == outTarget) {
                        row.outCombo->setCurrentIndex(c);
                        break;
                    }
                }
                return;
            }
            int oldVal = m_project.instruments()[instIndex].outputBusIndex();
            emit outputWillChange(instIndex, oldVal, targetBusIdx);
            m_project.instruments()[instIndex].setOutputBusIndex(targetBusIdx);
            emit instrumentChanged();
        });

        connect(row.panSlider, &QSlider::valueChanged, this, [this, instIndex](int val) {
            float oldVal = m_project.instruments()[instIndex].pan();
            float newVal = val / 100.0f;
            emit panWillChange(instIndex, oldVal, newVal);
            m_project.instruments()[instIndex].setPan(newVal);
            emit instrumentChanged();
        });

        connect(row.volumeSlider, &QSlider::valueChanged, this, [this, instIndex](int val) {
            float oldVal = m_project.instruments()[instIndex].volume();
            float newVal = val / 100.0f;
            emit volumeWillChange(instIndex, oldVal, newVal);
            m_project.instruments()[instIndex].setVolume(newVal);
            emit instrumentChanged();
        });

        m_containerLayout->addWidget(row.widget);
        m_instrumentRows.push_back(row);
    }

    m_containerLayout->addStretch();
}

void InstrumentPanelWidget::openSynthDialog(int index) {
    if (!m_pluginManager) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Select Instrument");
    dialog.setMinimumSize(420, 300);

    auto* layout = new QVBoxLayout(&dialog);
    auto* searchEdit = new QLineEdit(&dialog);
    searchEdit->setPlaceholderText("Search instruments...");
    searchEdit->setFocus();
    layout->addWidget(searchEdit);

    auto* listWidget = new QListWidget(&dialog);

    auto populateList = [this, listWidget](const QString& text) {
        QVector<QPair<int, const PluginInfo*>> scored;
        for (const auto& pi : m_pluginManager->plugins()) {
            if (!pi.isInstrument) continue;
            const QString display = QString("[%1] %2").arg(pi.type.toUpper(), pi.name);
            int score = 0;
            if (!text.isEmpty()) {
                score = -1;
                const QString lowered = display.toLower();
                const QString q = text.toLower();
                int pos = 0;
                for (const QChar& c : q) {
                    pos = lowered.indexOf(c, pos);
                    if (pos < 0) { score = -1; break; }
                    score += 20 - pos / 2;
                    ++pos;
                }
            }
            if (score >= 0)
                scored.append({score, &pi});
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const QPair<int, const PluginInfo*>& a,
                            const QPair<int, const PluginInfo*>& b) {
                             return a.first > b.first;
                         });
        listWidget->clear();
        for (const auto& entry : scored) {
            const PluginInfo* pi = entry.second;
            auto* item = new QListWidgetItem(QString("[%1] %2").arg(pi->type.toUpper(), pi->name));
            item->setData(Qt::UserRole, pi->type);
            item->setData(Qt::UserRole + 1, pi->path);
            listWidget->addItem(item);
        }
        if (listWidget->count() > 0)
            listWidget->setCurrentItem(listWidget->item(0));
    };
    populateList(QString());

    layout->addWidget(listWidget);

    auto* buttons = new QHBoxLayout;
    auto* okBtn = new QPushButton("Select", &dialog);
    auto* cancelBtn = new QPushButton("Cancel", &dialog);
    buttons->addWidget(okBtn);
    buttons->addWidget(cancelBtn);
    layout->addLayout(buttons);

    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(searchEdit, &QLineEdit::textChanged, listWidget, populateList);
    connect(searchEdit, &QLineEdit::returnPressed, &dialog, [listWidget, &dialog] {
        if (listWidget->count() > 0)
            dialog.accept();
    });

    if (dialog.exec() == QDialog::Accepted && listWidget->currentItem()) {
        auto* item = listWidget->currentItem();
        QString type = item->data(Qt::UserRole).toString();
        QString path = item->data(Qt::UserRole + 1).toString();
        emit synthAddRequested(index, type, path);
    }
}

bool InstrumentPanelWidget::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (auto& row : m_instrumentRows) {
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
        for (int i = 0; i < static_cast<int>(m_instrumentRows.size()); ++i) {
            if (m_instrumentRows[i].widget == obj) {
                QMenu menu(m_instrumentRows[i].widget);
                QAction* deleteAction = menu.addAction("Delete Instrument");
                connect(deleteAction, &QAction::triggered, this, [this, i] {
                    emit removeInstrumentRequested(i);
                });
                menu.exec(ce->globalPos());
                return true;
            }
        }
        if (obj == m_container) {
            QMenu menu(m_container);
            QAction* addAction = menu.addAction("Add Instrument");
            connect(addAction, &QAction::triggered, this, [this] {
                emit addInstrumentRequested();
            });
            menu.exec(ce->globalPos());
            return true;
        }
    }
    return QScrollArea::eventFilter(obj, event);
}
