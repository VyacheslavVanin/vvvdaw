#pragma once
#include <QMainWindow>
#include <QScrollBar>
#include <memory>
#include <vector>

#include "core/UndoStack.h"

class Project;
class AudioEngine;
class Settings;
class TransportPanel;
class TimelineRuler;
class TrackPanelWidget;
class TrackViewWidget;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Project& project, AudioEngine& engine, Settings& settings,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupMenus();
    void loadStyleSheet();
    void rebuildTracks();
    void syncScrollPositions(int value);
    void pushUndoState();
    void performUndo();
    void performRedo();

    Project& m_project;
    AudioEngine& m_engine;
    Settings& m_settings;
    UndoStack m_undoStack;

    TransportPanel* m_transportPanel = nullptr;
    TimelineRuler* m_timelineRuler = nullptr;
    QScrollBar* m_horizontalScroll = nullptr;
    QWidget* m_trackContainer = nullptr;
    QVBoxLayout* m_trackLayout = nullptr;

    struct TrackRow {
        TrackPanelWidget* panel = nullptr;
        TrackViewWidget* view = nullptr;
    };
    std::vector<TrackRow> m_trackRows;

    double m_zoom = 0.001;
    int64_t m_scrollOffset = 0;
};
