#pragma once
#include <QWidget>
#include <QJsonObject>
#include <cstdint>

class Project;
class UndoStack;
class PianoRollWidget;
class VelocityEditorWidget;
class ControlEventEditorWidget;
class AudioEngine;
class QComboBox;

class PianoRollWindow : public QWidget {
    Q_OBJECT
public:
    PianoRollWindow(Project& project, UndoStack& undo, AudioEngine& engine,
                    int trackIndex, int64_t eventId, QWidget* parent = nullptr);
    ~PianoRollWindow() override;

    int trackIndex() const { return m_trackIndex; }
    int64_t eventId() const { return m_eventId; }

    bool reload();
    void setPlayheadSample(int64_t sample);
    void closeEvent(QCloseEvent* event) override;

signals:
    void windowClosed();
    void undoRequested();
    void redoRequested();
    void toggleSnapRequested();

private:
    // Toolbar wiring (kept out of the constructor to stay flat): control-lane
    // selector and the MIDI output channel combo.
    void setupLaneSelector(class QComboBox* ctrlCombo, class QSpinBox* ccSpin);
    void setupChannelSelector(class QComboBox* channelCombo);
    void applyLaneSelection(int kindData, class QComboBox* ctrlCombo, class QSpinBox* ccSpin);
    void applyCustomCc(int ccNumber, class QComboBox* ctrlCombo, class QSpinBox* ccSpin);

    Project& m_project;
    UndoStack& m_undo;
    AudioEngine& m_engine;
    int m_trackIndex;
    int64_t m_eventId;
    PianoRollWidget* m_widget = nullptr;
    VelocityEditorWidget* m_velocityEditor = nullptr;
    ControlEventEditorWidget* m_controlEditor = nullptr;
    QComboBox* m_channelCombo = nullptr;
    bool m_syncingScroll = false;
};
