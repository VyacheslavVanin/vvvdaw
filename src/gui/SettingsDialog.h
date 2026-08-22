#pragma once
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <vector>
#include "core/Settings.h"

class AudioEngine;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Settings& settings, AudioEngine& engine, QWidget* parent = nullptr);

private:
    void populateSampleRates();
    void populateBufferSizes();
    void populateDevices();
    void populateMidiInputDevices();
    void accept() override;

    Settings& m_settings;
    AudioEngine& m_engine;

    QComboBox* m_sampleRateCombo;
    QComboBox* m_bufferSizeCombo;
    QComboBox* m_inputDeviceCombo;
    QComboBox* m_outputDeviceCombo;
    QComboBox* m_midiInputCombo;
    QComboBox* m_midiTransportTypeCombo;
    QSpinBox* m_midiPlaySpin;
    QSpinBox* m_midiRecordSpin;
    QSpinBox* m_midiStopSpin;
    QSpinBox* m_inputChannelSpin;
    QSpinBox* m_outputChannelSpin;
    QSpinBox* m_streamingThresholdSpin;
    QCheckBox* m_mouseWheelCheck;
    QSpinBox* m_knobsPerRowSpin;
    QListWidget* m_pluginPathList;
};
