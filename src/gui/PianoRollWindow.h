#pragma once
#include <QWidget>
#include <QJsonObject>
#include <cstdint>

class Project;
class UndoStack;
class PianoRollWidget;
class AudioEngine;

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

private:
    Project& m_project;
    AudioEngine& m_engine;
    int m_trackIndex;
    int64_t m_eventId;
    PianoRollWidget* m_widget = nullptr;
};
