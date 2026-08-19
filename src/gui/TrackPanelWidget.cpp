#include "TrackPanelWidget.h"
#include "PanSlider.h"
#include "plugin/PluginInstance.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "audio/DeviceInfo.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>

namespace {
int midiComboEncodeDevice(int deviceId) { return deviceId + 1; }
int midiComboEncodeInstrument(int index) { return -(index + 1); }
}

TrackPanelWidget::TrackPanelWidget(Track* track, QWidget* parent)
    : QWidget(parent)
    , m_track(track)
{
    setFixedWidth(200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 2, 3, 2);
    layout->setSpacing(1);

    auto* topRow = new QHBoxLayout;
    m_nameEdit = new QLineEdit(track ? track->name() : "Track", this);
    m_nameEdit->setReadOnly(true);
    m_nameEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: none; font-weight: bold; font-size: 11px; color: #ccc; }"
        "QLineEdit:focus { background: #333; border: 1px solid #6688cc; }"
    );
    m_nameEdit->installEventFilter(this);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_track) return;
        QString text = m_nameEdit->text().trimmed();
        if (text.isEmpty()) {
            m_nameEdit->setText(m_track->name());
        } else if (text != m_track->name()) {
            emit beforeModify();
            m_track->setName(text);
        }
        m_nameEdit->setReadOnly(true);
        m_nameEdit->setSelection(0, 0);
    });
    topRow->addWidget(m_nameEdit, 1);

    m_channelsBadge = new QLabel(this);
    m_channelsBadge->setFixedWidth(16);
    m_channelsBadge->setAlignment(Qt::AlignCenter);
    m_channelsBadge->setStyleSheet(
        "QLabel { color: #8899aa; font-size: 9px; font-weight: bold; border: 1px solid #445566; border-radius: 2px; padding: 0px; }"
    );
    m_channelsBadge->setToolTip("Track channel count");
    topRow->addWidget(m_channelsBadge);

    auto makeBtn = [&](const QString& text, const QString& style) {
        auto* btn = new QPushButton(text, this);
        btn->setFixedSize(20, 16);
        btn->setCheckable(true);
        btn->setStyleSheet(style);
        topRow->addWidget(btn);
        return btn;
    };

    auto btnStyle = [](const QString& normal, const QString& checked) {
        return normal + "; padding: 0px; }"
             + checked + "; padding: 0px; }";
    };

    m_armButton = makeBtn(QString::fromUtf8("\xe2\x97\x8f"),
        btnStyle(
            "QPushButton { background: #442222; color: #cc6666; border: 1px solid #664444; font-weight: bold; font-size: 12px",
            "QPushButton:checked { background: #cc2222; color: white; border: 2px solid #ff4444; font-weight: bold; font-size: 12px"
        ));
    m_armButton->setToolTip("Record Arm");

    m_soloButton = makeBtn("S",
        btnStyle(
            "QPushButton { background: #443322; color: #ccaa66; border: 1px solid #665544; font-weight: bold; font-size: 10px",
            "QPushButton:checked { background: #cc8800; color: white; border: 2px solid #ffaa00; font-weight: bold; font-size: 10px"
        ));
    m_soloButton->setToolTip("Solo");

    m_muteButton = makeBtn("M",
        btnStyle(
            "QPushButton { background: #334433; color: #66cc66; border: 1px solid #446644; font-weight: bold; font-size: 10px",
            "QPushButton:checked { background: #33aa33; color: white; border: 2px solid #44ff44; font-weight: bold; font-size: 10px"
        ));
    m_muteButton->setToolTip("Mute");

    m_monitorButton = makeBtn("MON",
        btnStyle(
            "QPushButton { background: #223344; color: #6688cc; border: 1px solid #445566; font-weight: bold; font-size: 9px",
            "QPushButton:checked { background: #2244aa; color: white; border: 2px solid #4488ff; font-weight: bold; font-size: 9px"
        ));
    m_monitorButton->setToolTip("Input Monitoring");

    layout->addLayout(topRow);

    auto* panRowWidget = new QWidget(this);
    m_panRow = panRowWidget;
    auto* panRow = new QHBoxLayout(panRowWidget);
    panRow->setContentsMargins(0, 0, 0, 0);
    auto* panLabel = new QLabel("pan:", panRowWidget);
    panLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    panRow->addWidget(panLabel);
    m_panSlider = new PanSlider(panRowWidget);
    panRow->addWidget(m_panSlider, 1);
    layout->addWidget(panRowWidget);

    auto* volRowWidget = new QWidget(this);
    m_volRow = volRowWidget;
    auto* volRow = new QHBoxLayout(volRowWidget);
    volRow->setContentsMargins(0, 0, 0, 0);
    auto* volLabel = new QLabel("level:", volRowWidget);
    volLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    volRow->addWidget(volLabel);
    m_volumeSlider = new QSlider(Qt::Horizontal, volRowWidget);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedHeight(18);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 5px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #aaa; width: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: #44aa44; border-radius: 2px; }"
    );
    m_volumeSlider->setToolTip("Volume: 80%");
    volRow->addWidget(m_volumeSlider, 1);
    layout->addWidget(volRowWidget);

    auto* inRowWidget = new QWidget(this);
    m_inRow = inRowWidget;
    auto* inRow = new QHBoxLayout(inRowWidget);
    inRow->setContentsMargins(0, 0, 0, 0);
    auto* inLabel = new QLabel("in: ", inRowWidget);
    inLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    inRow->addWidget(inLabel);
    m_inputDeviceCombo = new QComboBox(inRowWidget);
    m_inputDeviceCombo->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 10px; padding: 1px 4px; }"
        "QComboBox::drop-down { border: none; width: 14px; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
    );
    inRow->addWidget(m_inputDeviceCombo, 1);
    layout->addWidget(inRowWidget);

    auto* outRow = new QHBoxLayout;
    auto* outLabel = new QLabel("out:", this);
    outLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    outRow->addWidget(outLabel);
    m_outputBusCombo = new QComboBox(this);
    m_outputBusCombo->setStyleSheet(
        "QComboBox { background: #333; color: #ccc; border: 1px solid #555; font-size: 10px; padding: 1px 4px; }"
        "QComboBox::drop-down { border: none; width: 14px; }"
        "QComboBox QAbstractItemView { background: #333; color: #ccc; selection-background-color: #094771; }"
    );
    outRow->addWidget(m_outputBusCombo, 1);
    layout->addLayout(outRow);

    connect(m_armButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) {
            bool oldValue = m_track->isRecordArmed();
            m_track->setRecordArmed(checked);
            emit armToggled(oldValue, checked);
        }
    });
    connect(m_soloButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) {
            bool oldValue = m_track->isSolo();
            m_track->setSolo(checked);
            emit soloToggled(oldValue, checked);
        }
    });
    connect(m_muteButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) {
            bool oldValue = m_track->isMuted();
            m_track->setMuted(checked);
            emit muteToggled(oldValue, checked);
        }
    });
    connect(m_monitorButton, &QPushButton::toggled, this, [this](bool checked) {
        if (m_track) {
            bool oldValue = m_track->isMonitoring();
            m_track->setMonitoring(checked);
            emit monitorToggled(oldValue, checked);
        }
    });
    connect(m_panSlider, &QSlider::valueChanged, this, [this](int val) {
        float pan = val / 100.0f;
        if (m_track) {
            float oldValue = m_track->pan();
            m_track->setPan(pan);
            emit panChanged(oldValue, pan);
        }
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        float vol = val / 100.0f;
        m_volumeSlider->setToolTip(QString("Volume: %1%").arg(val));
        if (m_track) {
            float oldValue = m_track->volume();
            m_track->setVolume(vol);
            emit volumeChanged(oldValue, vol);
        }
    });

    connect(m_outputBusCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (!m_track) return;
        if (m_track->type() == Track::Type::Midi) {
            emit beforeModify();
            int encoded = m_outputBusCombo->itemData(index).toInt();
            if (encoded > 0) {
                int deviceId = encoded - 1;
                QString deviceName = m_outputBusCombo->currentText();
                m_track->setMidiOutputDeviceId(deviceId);
                m_track->setMidiOutputDeviceName(deviceName);
                m_track->setInstrumentIndex(-1);
                emit midiOutputChanged(deviceId, deviceName, -1);
            } else if (encoded < 0) {
                int instIndex = -encoded - 1;
                m_track->setInstrumentIndex(instIndex);
                m_track->setMidiOutputDeviceId(-1);
                m_track->setMidiOutputDeviceName(QString());
                emit midiOutputChanged(-1, QString(), instIndex);
            } else {
                m_track->setInstrumentIndex(-1);
                m_track->setMidiOutputDeviceId(-1);
                m_track->setMidiOutputDeviceName(QString());
                emit midiOutputChanged(-1, QString(), -1);
            }
            return;
        }
        int oldIndex = m_track->outputBusIndex();
        m_track->setOutputBusIndex(index);
        emit outputBusChanged(oldIndex, index);
    });
    connect(m_inputDeviceCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_track) {
            emit beforeModify();
            int deviceId = m_inputDeviceCombo->currentData().toInt();
            m_track->setInputDeviceId(deviceId);
            emit inputDeviceChanged(deviceId);
        }
    });

    applyTrackType();
}

void TrackPanelWidget::applyTrackType() {
    bool isMidi = m_track && m_track->type() == Track::Type::Midi;
    m_panRow->setVisible(!isMidi);
    m_volRow->setVisible(!isMidi);
    m_inRow->setVisible(!isMidi);
    m_armButton->setVisible(!isMidi);
    m_monitorButton->setVisible(!isMidi);
    m_channelsBadge->setText(isMidi ? "MIDI" : (m_track && m_track->channels() == 1 ? "M" : "S"));
    m_channelsBadge->setFixedWidth(isMidi ? 30 : 16);
}

void TrackPanelWidget::updateFromTrack() {
    if (!m_track) return;
    m_nameEdit->setText(m_track->name());
    applyTrackType();
    m_soloButton->setChecked(m_track->isSolo());
    m_muteButton->setChecked(m_track->isMuted());
    if (m_track->type() != Track::Type::Midi) {
        m_armButton->setChecked(m_track->isRecordArmed());
        m_monitorButton->setChecked(m_track->isMonitoring());
        m_panSlider->setValue(static_cast<int>(m_track->pan() * 100));
        m_volumeSlider->setValue(static_cast<int>(m_track->volume() * 100));
    }

    if (m_track->type() == Track::Type::Midi) {
        // Select current routing in the MIDI out combo, if present.
        int targetEncoded = 0;
        if (m_track->instrumentIndex() >= 0)
            targetEncoded = midiComboEncodeInstrument(m_track->instrumentIndex());
        else if (m_track->midiOutputDeviceId() >= 0)
            targetEncoded = midiComboEncodeDevice(m_track->midiOutputDeviceId());
        for (int i = 0; i < m_outputBusCombo->count(); ++i) {
            if (m_outputBusCombo->itemData(i).toInt() == targetEncoded) {
                m_outputBusCombo->setCurrentIndex(i);
                break;
            }
        }
        return;
    }

    int busIdx = m_track->outputBusIndex();
    if (busIdx >= 0 && busIdx < m_outputBusCombo->count())
        m_outputBusCombo->setCurrentIndex(busIdx);
}

void TrackPanelWidget::updateMidiOutputs(const std::vector<std::pair<int, QString>>& devices,
                                         const std::vector<QString>& instrumentNames) {
    if (m_track && m_track->type() != Track::Type::Midi)
        return;
    QSignalBlocker blocker(m_outputBusCombo);
    m_outputBusCombo->clear();
    m_outputBusCombo->addItem("None");
    m_outputBusCombo->setItemData(0, 0);

    int selectIdx = 0;
    if (m_track && m_track->midiOutputDeviceId() < 0 && m_track->instrumentIndex() < 0)
        selectIdx = 0;

    for (const auto& [id, name] : devices) {
        m_outputBusCombo->addItem("MIDI: " + name);
        m_outputBusCombo->setItemData(m_outputBusCombo->count() - 1, midiComboEncodeDevice(id));
        if (m_track && m_track->midiOutputDeviceId() == id)
            selectIdx = m_outputBusCombo->count() - 1;
    }
    for (int i = 0; i < static_cast<int>(instrumentNames.size()); ++i) {
        m_outputBusCombo->addItem("Inst: " + instrumentNames[i]);
        m_outputBusCombo->setItemData(m_outputBusCombo->count() - 1, midiComboEncodeInstrument(i));
        if (m_track && m_track->instrumentIndex() == i)
            selectIdx = m_outputBusCombo->count() - 1;
    }

    m_outputBusCombo->setCurrentIndex(selectIdx);
}

void TrackPanelWidget::updateBusList(const std::vector<AudioBus>& buses) {
    if (m_track && m_track->type() == Track::Type::Midi)
        return;
    QSignalBlocker blocker(m_outputBusCombo);
    m_outputBusCombo->clear();
    for (const auto& bus : buses) {
        m_outputBusCombo->addItem(bus.name());
    }
    if (m_track) {
        int idx = m_track->outputBusIndex();
        if (idx >= 0 && idx < m_outputBusCombo->count())
            m_outputBusCombo->setCurrentIndex(idx);
    }
}

void TrackPanelWidget::updateInputDeviceList(const std::vector<DeviceInfo>& devices) {
    QSignalBlocker blocker(m_inputDeviceCombo);
    m_inputDeviceCombo->clear();
    m_inputDeviceCombo->addItem("None");
    m_inputDeviceCombo->setItemData(0, -1);
    int comboIdx = 1;
    int currentDeviceId = m_track ? m_track->inputDeviceId() : -1;
    int selectIdx = 0;
    for (const auto& dev : devices) {
        m_inputDeviceCombo->addItem(dev.name);
        m_inputDeviceCombo->setItemData(comboIdx, dev.id);
        if (dev.id == currentDeviceId)
            selectIdx = comboIdx;
        ++comboIdx;
    }
    m_inputDeviceCombo->setCurrentIndex(selectIdx);
}

void TrackPanelWidget::setAlternateRow(bool alternate) {
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, alternate ? QColor("#2f2f2f") : QColor("#2a2a2a"));
    setPalette(p);
}

bool TrackPanelWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_nameEdit && event->type() == QEvent::MouseButtonDblClick) {
        m_nameEdit->setReadOnly(false);
        m_nameEdit->selectAll();
        m_nameEdit->setFocus();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void TrackPanelWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* addStereoAction = menu.addAction("Add Stereo Track");
    connect(addStereoAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit addTrackRequested(2); }, Qt::QueuedConnection);
    });
    QAction* addMonoAction = menu.addAction("Add Mono Track");
    connect(addMonoAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit addTrackRequested(1); }, Qt::QueuedConnection);
    });
    QAction* addMidiAction = menu.addAction("Add MIDI Track");
    connect(addMidiAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit addMidiTrackRequested(); }, Qt::QueuedConnection);
    });
    if (m_track && m_track->type() == Track::Type::Midi) {
        QAction* addMidiEventAction = menu.addAction("Add MIDI Event");
        connect(addMidiEventAction, &QAction::triggered, this, [this] {
            QMetaObject::invokeMethod(this, [this] { emit addMidiEventRequested(); }, Qt::QueuedConnection);
        });
    }
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Delete Track");
    connect(deleteAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(this, [this] { emit deleteRequested(); }, Qt::QueuedConnection);
    });
    menu.exec(event->globalPos());
}
