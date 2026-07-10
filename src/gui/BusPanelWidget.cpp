#include "BusPanelWidget.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QEvent>

BusPanelWidget::BusPanelWidget(Project& project, QWidget* parent)
    : QScrollArea(parent)
    , m_project(project)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setWidgetResizable(true);
    setFixedHeight(180);

    m_container = new QWidget(this);
    m_container->setAutoFillBackground(true);
    QPalette pal = m_container->palette();
    pal.setColor(QPalette::Window, QColor("#252525"));
    m_container->setPalette(pal);

    auto* rootLayout = new QHBoxLayout(m_container);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    m_containerLayout = new QHBoxLayout;
    m_containerLayout->setContentsMargins(0, 0, 0, 0);
    m_containerLayout->setSpacing(4);
    rootLayout->addLayout(m_containerLayout, 1);

    m_addBusBtn = new QPushButton("+", m_container);
    m_addBusBtn->setFixedSize(28, 28);
    m_addBusBtn->setStyleSheet(
        "QPushButton { background: #3c3c3c; color: #88cc88; border: 1px solid #555; "
        "border-radius: 3px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #4a4a4a; }"
    );
    m_addBusBtn->setToolTip("Add Bus");
    rootLayout->addWidget(m_addBusBtn, 0, Qt::AlignTop);

    connect(m_addBusBtn, &QPushButton::clicked, this, [this] {
        emit addBusRequested();
    });

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

    const auto& buses = m_project.buses();

    for (int i = 0; i < static_cast<int>(buses.size()); ++i) {
        const auto& bus = buses[i];
        BusRow row;

        row.widget = new QWidget(m_container);
        row.widget->setFixedWidth(140);
        row.widget->setAutoFillBackground(true);
        QPalette wp = row.widget->palette();
        wp.setColor(QPalette::Window, (i % 2 == 0) ? QColor("#2e2e2e") : QColor("#333333"));
        row.widget->setPalette(wp);

        auto* layout = new QVBoxLayout(row.widget);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);

        auto* nameRow = new QHBoxLayout;
        row.nameEdit = new QLineEdit(bus.name, row.widget);
        row.nameEdit->setReadOnly(true);
        row.nameEdit->setStyleSheet(
            "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 11px; color: #ccc; }"
            "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
        );
        nameRow->addWidget(row.nameEdit, 1);

        bool isMaster = (i == 0);
        if (!isMaster) {
            row.deleteBtn = new QPushButton(QString::fromUtf8("\xc3\x97"), row.widget);
            row.deleteBtn->setFixedSize(18, 18);
            row.deleteBtn->setStyleSheet(
                "QPushButton { background: #442222; color: #cc6666; border: 1px solid #553333; "
                "border-radius: 2px; font-size: 10px; padding: 0px; }"
                "QPushButton:hover { background: #663333; }"
            );
            row.deleteBtn->setToolTip("Remove Bus");
            nameRow->addWidget(row.deleteBtn, 0, Qt::AlignRight);
        }

        layout->addLayout(nameRow);

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
        for (int j = 0; j < static_cast<int>(buses.size()); ++j) {
            if (j == i) continue;
            row.outCombo->addItem(buses[j].name, j);
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

        int busIndex = i;

        connect(row.nameEdit, &QLineEdit::editingFinished, this, [this, row, busIndex] {
            QString text = row.nameEdit->text().trimmed();
            if (!text.isEmpty() && text != m_project.buses()[busIndex].name) {
                m_project.buses()[busIndex].name = text;
                emit busChanged();
            }
            row.nameEdit->setReadOnly(true);
        });

        row.nameEdit->installEventFilter(this);

        connect(row.outCombo, QOverload<int>::of(&QComboBox::activated), this,
                [this, row, busIndex](int) {
            int targetBusIdx = row.outCombo->currentData().toInt();
            m_project.buses()[busIndex].outputBusIndex = targetBusIdx;
            emit busChanged();
        });

        connect(row.panSlider, &QSlider::valueChanged, this,
                [this, busIndex](int val) {
            m_project.buses()[busIndex].pan = val / 100.0f;
            emit busChanged();
        });

        connect(row.volumeSlider, &QSlider::valueChanged, this,
                [this, busIndex](int val) {
            m_project.buses()[busIndex].volume = val / 100.0f;
            emit busChanged();
        });

        if (row.deleteBtn) {
            connect(row.deleteBtn, &QPushButton::clicked, this,
                    [this, busIndex] {
                emit removeBusRequested(busIndex);
            });
        }

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
    return QScrollArea::eventFilter(obj, event);
}
