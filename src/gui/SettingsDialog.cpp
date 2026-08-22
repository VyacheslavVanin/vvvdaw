#include "SettingsDialog.h"
#include "audio/AudioEngine.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QFileDialog>
#include <QLabel>

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

    auto* transportRow = new QWidget(this);
    auto* transportLayout = new QHBoxLayout(transportRow);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(4);
    m_midiTransportTypeCombo = new QComboBox(this);
    m_midiTransportTypeCombo->setObjectName("midiTransportTypeCombo");
    m_midiTransportTypeCombo->addItem("None", 0);
    m_midiTransportTypeCombo->addItem("CC", 1);
    m_midiTransportTypeCombo->addItem("Note", 2);
    m_midiTransportTypeCombo->setCurrentIndex(
        m_settings.midiTransportControlType >= 0 && m_settings.midiTransportControlType <= 2
            ? m_settings.midiTransportControlType : 0);
    m_midiTransportTypeCombo->setToolTip("MIDI message type used for transport control");
    m_midiPlaySpin = makeControlSpin(m_settings.midiTransportPlayControl, "Play");
    m_midiPlaySpin->setObjectName("midiPlaySpin");
    m_midiRecordSpin = makeControlSpin(m_settings.midiTransportRecordControl, "Record");
    m_midiRecordSpin->setObjectName("midiRecordSpin");
    m_midiStopSpin = makeControlSpin(m_settings.midiTransportStopControl, "Stop");
    m_midiStopSpin->setObjectName("midiStopSpin");
    transportLayout->addWidget(m_midiTransportTypeCombo);
    transportLayout->addWidget(m_midiPlaySpin);
    transportLayout->addWidget(m_midiRecordSpin);
    transportLayout->addWidget(m_midiStopSpin);
    transportLayout->addStretch();
    form->addRow("MIDI Transport:", transportRow);

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
