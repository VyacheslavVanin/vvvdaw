#pragma once
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <vector>
#include "core/Settings.h"
#include "midi/MidiInputManager.h"

class AudioEngine;
class QPushButton;
class QTimer;
class QLabel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Settings& settings, AudioEngine& engine, QWidget* parent = nullptr);

private:
    void populateSampleRates();
    void populateBufferSizes();
    void populateDevices();
    void populateMidiInputDevices();
    void toggleLearn(MidiLearnTarget target, QPushButton* btn);
    void cancelLearn();
    void applyLearned(const MidiTransportControls& c);
    void accept() override;
    void reject() override;

    Settings& m_settings;
    AudioEngine& m_engine;

    QComboBox* m_sampleRateCombo;
    QComboBox* m_bufferSizeCombo;
    QComboBox* m_inputDeviceCombo;
    QComboBox* m_outputDeviceCombo;
    QComboBox* m_midiInputCombo;
    QComboBox* m_midiTransportTypeCombo;
    QComboBox* m_midiChannelCombo;
    QSpinBox* m_midiPlaySpin;
    QSpinBox* m_midiRecordSpin;
    QSpinBox* m_midiStopSpin;
    QPushButton* m_learnPlayBtn = nullptr;
    QPushButton* m_learnRecordBtn = nullptr;
    QPushButton* m_learnStopBtn = nullptr;
    QLabel* m_learnStatusLabel = nullptr;
    QTimer* m_learnTimer = nullptr;
    MidiLearnTarget m_learnTarget = MidiLearnTarget::None;
    QPushButton* m_activeLearnBtn = nullptr;
    int m_learnTimeoutMs = 0;
    int m_midiTransportKind = 0; // precise status & 0xF0 of the learned message
    QSpinBox* m_inputChannelSpin;
    QSpinBox* m_outputChannelSpin;
    QSpinBox* m_streamingThresholdSpin;
    QSpinBox* m_knobsPerRowSpin;
    QListWidget* m_pluginPathList;
};
