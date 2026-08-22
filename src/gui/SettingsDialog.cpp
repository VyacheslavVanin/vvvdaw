#include "SettingsDialog.h"
#include "audio/AudioEngine.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QFileDialog>
#include <QLabel>
#include <QTimer>

namespace {
QSpinBox* makeControlSpin(int value, const QString& tooltip, QWidget* parent = nullptr) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(1, 127);
    spin->setValue(value);
    spin->setToolTip(tooltip);
    return spin;
}
} // namespace

SettingsDialog::SettingsDialog(Settings& settings, AudioEngine& engine, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_engine(engine)
{
    setWindowTitle("Settings");
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout;

    m_sampleRateCombo = new QComboBox(this);
    populateSampleRates();
    form->addRow("Sample Rate:", m_sampleRateCombo);

    m_bufferSizeCombo = new QComboBox(this);
    populateBufferSizes();
    form->addRow("Buffer Size:", m_bufferSizeCombo);

    m_inputDeviceCombo = new QComboBox(this);
    m_outputDeviceCombo = new QComboBox(this);
    populateDevices();
    form->addRow("Input Device:", m_inputDeviceCombo);
    form->addRow("Output Device:", m_outputDeviceCombo);

    m_midiInputCombo = new QComboBox(this);
    m_midiInputCombo->setObjectName("midiInputCombo");
    populateMidiInputDevices();
    form->addRow("MIDI Input Device:", m_midiInputCombo);

    auto* transportGroup = new QWidget(this);
    auto* transportLayout = new QVBoxLayout(transportGroup);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(3);

    auto* typeRow = new QHBoxLayout;
    auto* typeLabel = new QLabel("Type:", transportGroup);
    typeLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    typeRow->addWidget(typeLabel);
    m_midiTransportTypeCombo = new QComboBox(this);
    m_midiTransportTypeCombo->setObjectName("midiTransportTypeCombo");
    m_midiTransportTypeCombo->addItem("None", 0);
    m_midiTransportTypeCombo->addItem("CC", 1);
    m_midiTransportTypeCombo->addItem("Note", 2);
    m_midiTransportTypeCombo->setCurrentIndex(
        m_settings.midiTransportControlType >= 0 && m_settings.midiTransportControlType <= 2
            ? m_settings.midiTransportControlType : 0);
    m_midiTransportTypeCombo->setToolTip("MIDI message type used for transport control");
    // The precise message kind follows the coarse type unless a message was
    // learned (which may be any channel voice message, e.g. a pad note).
    m_midiTransportKind = m_settings.midiTransportKind;
    if (m_midiTransportKind == 0)
        m_midiTransportKind = m_settings.midiTransportControlType == 1 ? 0xB0 : 0x90;
    connect(m_midiTransportTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        switch (m_midiTransportTypeCombo->currentData().toInt()) {
        case 1: m_midiTransportKind = 0xB0; break;
        case 2: m_midiTransportKind = 0x90; break;
        default: m_midiTransportKind = 0; break;
        }
    });
    typeRow->addWidget(m_midiTransportTypeCombo, 1);

    auto* channelLabel = new QLabel("Ch:", transportGroup);
    channelLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    typeRow->addWidget(channelLabel);
    m_midiChannelCombo = new QComboBox(this);
    m_midiChannelCombo->setObjectName("midiChannelCombo");
    m_midiChannelCombo->addItem("All", -1);
    for (int ch = 1; ch <= 16; ++ch)
        m_midiChannelCombo->addItem(QString::number(ch), ch - 1);
    int channelIdx = 0;
    if (m_settings.midiTransportChannel >= 0 && m_settings.midiTransportChannel <= 15)
        channelIdx = m_settings.midiTransportChannel + 1;
    m_midiChannelCombo->setCurrentIndex(channelIdx);
    m_midiChannelCombo->setToolTip("MIDI channel the transport commands arrive on");
    typeRow->addWidget(m_midiChannelCombo);
    transportLayout->addLayout(typeRow);

    auto makeLearnRow = [&](const QString& label, QSpinBox*& spin, int value,
                            const QString& objName, const QString& btnObjName,
                            QPushButton*& btn) {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(label, transportGroup);
        lbl->setStyleSheet("color: #aaa; font-size: 11px;");
        row->addWidget(lbl);
        spin = makeControlSpin(value, label);
        spin->setObjectName(objName);
        row->addWidget(spin);
        btn = new QPushButton("Learn", transportGroup);
        btn->setObjectName(btnObjName);
        btn->setToolTip("Press the button on the MIDI device to assign it");
        btn->setStyleSheet(
            "QPushButton { background: #333; color: #ccc; border: 1px solid #555; font-size: 11px; padding: 1px 8px; }"
            "QPushButton:hover { background: #444; }"
        );
        row->addWidget(btn);
        transportLayout->addLayout(row);
    };

    makeLearnRow("Play:", m_midiPlaySpin, m_settings.midiTransportPlayControl,
                 "midiPlaySpin", "midiLearnPlayBtn", m_learnPlayBtn);
    makeLearnRow("Record:", m_midiRecordSpin, m_settings.midiTransportRecordControl,
                 "midiRecordSpin", "midiLearnRecordBtn", m_learnRecordBtn);
    makeLearnRow("Stop:", m_midiStopSpin, m_settings.midiTransportStopControl,
                 "midiStopSpin", "midiLearnStopBtn", m_learnStopBtn);

    // Shows the last learned message so it is obvious what the device sent.
    m_learnStatusLabel = new QLabel("", transportGroup);
    m_learnStatusLabel->setObjectName("midiLearnStatus");
    m_learnStatusLabel->setStyleSheet("color: #88cc88; font-size: 11px;");
    transportLayout->addWidget(m_learnStatusLabel);

    form->addRow("MIDI Transport:", transportGroup);

    connect(m_learnPlayBtn, &QPushButton::clicked, this, [this] {
        toggleLearn(MidiLearnTarget::Play, m_learnPlayBtn);
    });
    connect(m_learnRecordBtn, &QPushButton::clicked, this, [this] {
        toggleLearn(MidiLearnTarget::Record, m_learnRecordBtn);
    });
    connect(m_learnStopBtn, &QPushButton::clicked, this, [this] {
        toggleLearn(MidiLearnTarget::Stop, m_learnStopBtn);
    });

    m_learnTimer = new QTimer(this);
    m_learnTimer->setInterval(50);
    connect(m_learnTimer, &QTimer::timeout, this, [this] {
        if (m_learnTarget == MidiLearnTarget::None) {
            m_learnTimer->stop();
            return;
        }
        MidiTransportControls c;
        if (m_engine.popLearnedMidiControl(c)) {
            applyLearned(c);
            return;
        }
        m_learnTimeoutMs -= 50;
        if (m_learnTimeoutMs <= 0) {
            if (m_activeLearnBtn)
                m_activeLearnBtn->setToolTip("No MIDI message received — press the device button");
            cancelLearn();
        }
    });

    m_inputChannelSpin = new QSpinBox(this);
    m_inputChannelSpin->setRange(0, 64);
    m_inputChannelSpin->setValue(m_settings.inputChannel);
    form->addRow("Input Channel:", m_inputChannelSpin);

    m_outputChannelSpin = new QSpinBox(this);
    m_outputChannelSpin->setRange(0, 64);
    m_outputChannelSpin->setValue(m_settings.outputChannel);
    form->addRow("Output Channel:", m_outputChannelSpin);

    m_streamingThresholdSpin = new QSpinBox(this);
    m_streamingThresholdSpin->setRange(1, 600);
    m_streamingThresholdSpin->setSuffix(" sec");
    m_streamingThresholdSpin->setValue(m_settings.streamingThresholdSec);
    form->addRow("Stream Threshold:", m_streamingThresholdSpin);

    m_mouseWheelCheck = new QCheckBox(this);
    m_mouseWheelCheck->setChecked(m_settings.mouseWheelScroll);
    form->addRow("Mouse Wheel Scroll:", m_mouseWheelCheck);

    m_knobsPerRowSpin = new QSpinBox(this);
    m_knobsPerRowSpin->setRange(2, 6);
    m_knobsPerRowSpin->setValue(m_settings.pluginKnobsPerRow);
    form->addRow("Knobs Per Row:", m_knobsPerRowSpin);

    layout->addLayout(form);

    auto* pluginPathsGroup = new QWidget(this);
    auto* pluginPathsLayout = new QVBoxLayout(pluginPathsGroup);
    pluginPathsLayout->setContentsMargins(0, 0, 0, 0);
    pluginPathsLayout->setSpacing(4);

    auto* pathsLabel = new QLabel("Plugin Scan Paths:", pluginPathsGroup);
    pathsLabel->setStyleSheet("font-weight: bold; font-size: 11px; color: #ccc;");
    pluginPathsLayout->addWidget(pathsLabel);

    m_pluginPathList = new QListWidget(pluginPathsGroup);
    m_pluginPathList->setFixedHeight(120);
    m_pluginPathList->setStyleSheet(
        "QListWidget { background: #2a2a2a; color: #ccc; border: 1px solid #555; font-size: 11px; }"
        "QListWidget::item:selected { background: #094771; }"
    );
    for (const auto& path : m_settings.pluginScanPaths)
        m_pluginPathList->addItem(path);
    pluginPathsLayout->addWidget(m_pluginPathList);

    auto* pathsBtnRow = new QHBoxLayout;
    auto* addPathBtn = new QPushButton("Add...", pluginPathsGroup);
    auto* removePathBtn = new QPushButton("Remove", pluginPathsGroup);
    auto* defaultPathsBtn = new QPushButton("Reset Defaults", pluginPathsGroup);
    pathsBtnRow->addWidget(addPathBtn);
    pathsBtnRow->addWidget(removePathBtn);
    pathsBtnRow->addWidget(defaultPathsBtn);
    pathsBtnRow->addStretch();
    pluginPathsLayout->addLayout(pathsBtnRow);

    layout->addWidget(pluginPathsGroup);

    layout->addStretch();

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", this);
    auto* okBtn = new QPushButton("OK", this);
    okBtn->setDefault(true);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(addPathBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Plugin Directory");
        if (!dir.isEmpty()) {
            m_pluginPathList->addItem(dir);
        }
    });

    connect(removePathBtn, &QPushButton::clicked, this, [this] {
        auto* item = m_pluginPathList->currentItem();
        if (item) {
            delete m_pluginPathList->takeItem(m_pluginPathList->row(item));
        }
    });

    connect(defaultPathsBtn, &QPushButton::clicked, this, [this] {
        m_pluginPathList->clear();
        m_pluginPathList->addItem(QDir::homePath() + "/.vst3");
        m_pluginPathList->addItem("/usr/lib/vst3");
        m_pluginPathList->addItem("/usr/local/lib/vst3");
    });
}

void SettingsDialog::populateSampleRates() {
    const int rates[] = {44100, 48000, 88200, 96000, 192000};
    int idx = 0;
    for (int i = 0; i < 5; ++i) {
        m_sampleRateCombo->addItem(QString::number(rates[i]), rates[i]);
        if (rates[i] == m_settings.sampleRate)
            idx = i;
    }
    m_sampleRateCombo->setCurrentIndex(idx);
}

void SettingsDialog::populateBufferSizes() {
    const int sizes[] = {64, 128, 256, 512, 1024, 2048};
    int idx = 0;
    for (int i = 0; i < 6; ++i) {
        m_bufferSizeCombo->addItem(QString::number(sizes[i]), sizes[i]);
        if (sizes[i] == m_settings.bufferSize)
            idx = i;
    }
    m_bufferSizeCombo->setCurrentIndex(idx);
}

void SettingsDialog::populateDevices() {
    std::vector<DeviceInfo> inputs = AudioEngine::enumerateInputDevices();
    std::vector<DeviceInfo> outputs = AudioEngine::enumerateOutputDevices();

    m_inputDeviceCombo->addItem("Default", -1);
    int inputIdx = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        m_inputDeviceCombo->addItem(inputs[i].name, inputs[i].id);
        if (inputs[i].id == m_settings.inputDeviceId)
            inputIdx = static_cast<int>(i) + 1;
    }
    m_inputDeviceCombo->setCurrentIndex(inputIdx);

    m_outputDeviceCombo->addItem("Default", -1);
    int outputIdx = 0;
    for (size_t i = 0; i < outputs.size(); ++i) {
        m_outputDeviceCombo->addItem(outputs[i].name, outputs[i].id);
        if (outputs[i].id == m_settings.outputDeviceId)
            outputIdx = static_cast<int>(i) + 1;
    }
    m_outputDeviceCombo->setCurrentIndex(outputIdx);
}

void SettingsDialog::populateMidiInputDevices() {
    auto midiInputs = AudioEngine::enumerateMidiInputDevices();
    m_midiInputCombo->addItem("None", -1);
    int inputIdx = 0;
    for (size_t i = 0; i < midiInputs.size(); ++i) {
        m_midiInputCombo->addItem(midiInputs[i].name, midiInputs[i].id);
        if (midiInputs[i].id == m_settings.midiInputDeviceId)
            inputIdx = static_cast<int>(i) + 1;
    }
    m_midiInputCombo->setCurrentIndex(inputIdx);
}

void SettingsDialog::accept() {
    m_settings.sampleRate = m_sampleRateCombo->currentData().toInt();
    m_settings.bufferSize = m_bufferSizeCombo->currentData().toInt();
    m_settings.inputDeviceId = m_inputDeviceCombo->currentData().toInt();
    m_settings.outputDeviceId = m_outputDeviceCombo->currentData().toInt();
    m_settings.inputChannel = m_inputChannelSpin->value();
    m_settings.outputChannel = m_outputChannelSpin->value();
    m_settings.midiInputDeviceId = m_midiInputCombo->currentData().toInt();
    m_settings.midiTransportControlType = m_midiTransportTypeCombo->currentData().toInt();
    m_settings.midiTransportKind = m_midiTransportKind;
    m_settings.midiTransportChannel = m_midiChannelCombo->currentData().toInt();
    m_settings.midiTransportPlayControl = m_midiPlaySpin->value();
    m_settings.midiTransportRecordControl = m_midiRecordSpin->value();
    m_settings.midiTransportStopControl = m_midiStopSpin->value();
    m_settings.streamingThresholdSec = m_streamingThresholdSpin->value();
    m_settings.mouseWheelScroll = m_mouseWheelCheck->isChecked();
    m_settings.pluginKnobsPerRow = m_knobsPerRowSpin->value();

    m_settings.pluginScanPaths.clear();
    for (int i = 0; i < m_pluginPathList->count(); ++i)
        m_settings.pluginScanPaths.push_back(m_pluginPathList->item(i)->text());

    m_settings.save();

    QDialog::accept();
}

void SettingsDialog::toggleLearn(MidiLearnTarget target, QPushButton* btn) {
    if (m_learnTarget != MidiLearnTarget::None) {
        bool same = (btn == m_activeLearnBtn);
        cancelLearn();
        if (same)
            return;
    }

    int deviceId = m_midiInputCombo->currentData().toInt();
    if (deviceId < 0) {
        btn->setToolTip("Select a MIDI input device first");
        return;
    }

    m_engine.setMidiInputDevice(deviceId);
    m_learnTarget = target;
    m_activeLearnBtn = btn;
    m_learnTimeoutMs = 10000;
    m_engine.setMidiLearnTarget(target);

    m_midiTransportTypeCombo->setEnabled(false);
    m_learnPlayBtn->setEnabled(false);
    m_learnRecordBtn->setEnabled(false);
    m_learnStopBtn->setEnabled(false);
    btn->setEnabled(true); // keep the active one clickable to cancel
    btn->setText("Press...");
    m_learnTimer->start();
}

void SettingsDialog::cancelLearn() {
    m_learnTimer->stop();
    m_engine.setMidiLearnTarget(MidiLearnTarget::None);
    m_learnTarget = MidiLearnTarget::None;
    m_midiTransportTypeCombo->setEnabled(true);
    m_learnPlayBtn->setEnabled(true);
    m_learnRecordBtn->setEnabled(true);
    m_learnStopBtn->setEnabled(true);
    if (m_activeLearnBtn) {
        m_activeLearnBtn->setText("Learn");
        m_activeLearnBtn->setToolTip("Press the button on the MIDI device to assign it");
    }
    m_activeLearnBtn = nullptr;
}

void SettingsDialog::applyLearned(const MidiTransportControls& c) {
    int typeIdx = 0;
    if (c.type == 1) typeIdx = 1;
    else if (c.type == 2) typeIdx = 2;
    m_midiTransportTypeCombo->setCurrentIndex(typeIdx);
    // Remember the exact message kind so transport matches the same message
    // family (note, note-off, poly pressure, program change, ...) even when the
    // coarse combo only shows "CC"/"Note".
    m_midiTransportKind = c.kind;

    // The learned message arrived on a concrete channel: show it so transport
    // commands are only accepted from that channel (user can revert to "All").
    if (c.channel >= 0 && c.channel <= 15)
        m_midiChannelCombo->setCurrentIndex(c.channel + 1);

    QSpinBox* spin = nullptr;
    int value = 0;
    switch (m_learnTarget) {
    case MidiLearnTarget::Play: spin = m_midiPlaySpin; value = c.play; break;
    case MidiLearnTarget::Record: spin = m_midiRecordSpin; value = c.record; break;
    case MidiLearnTarget::Stop: spin = m_midiStopSpin; value = c.stop; break;
    default: break;
    }
    if (spin)
        spin->setValue(value);

    if (m_learnStatusLabel) {
        QString kindName;
        switch (c.kind & 0xF0) {
        case 0x80: kindName = "Note Off"; break;
        case 0x90: kindName = "Note"; break;
        case 0xA0: kindName = "Poly Pressure"; break;
        case 0xB0: kindName = "CC"; break;
        case 0xC0: kindName = "Program Change"; break;
        case 0xD0: kindName = "Channel Pressure"; break;
        default: kindName = "MIDI"; break;
        }
        QString ch = (c.channel >= 0 && c.channel <= 15)
                         ? QString(" Ch %1").arg(c.channel + 1) : QString();
        m_learnStatusLabel->setText(QString("%1 %2%3")
            .arg(kindName).arg(value).arg(ch));
    }

    QPushButton* learned = m_activeLearnBtn;
    cancelLearn();
    if (learned) {
        learned->setText(QString::fromUtf8("\xe2\x9c\x93")); // check mark
        QTimer::singleShot(800, learned, [learned] { learned->setText("Learn"); });
    }
}

void SettingsDialog::reject() {
    cancelLearn();
    // Undo any live device switch done for learning, unless it matches the
    // saved device (accept() restarts the engine from the new settings).
    m_engine.setMidiInputDevice(m_settings.midiInputDeviceId);
    QDialog::reject();
}
