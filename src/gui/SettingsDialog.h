#pragma once
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
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
    void accept() override;

    Settings& m_settings;
    AudioEngine& m_engine;

    QComboBox* m_sampleRateCombo;
    QComboBox* m_bufferSizeCombo;
    QComboBox* m_inputDeviceCombo;
    QComboBox* m_outputDeviceCombo;
    QSpinBox* m_inputChannelSpin;
    QSpinBox* m_outputChannelSpin;
    QSpinBox* m_streamingThresholdSpin;
};
