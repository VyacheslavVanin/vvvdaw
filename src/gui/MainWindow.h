#pragma once
#include <QMainWindow>
#include <QScrollBar>
#include <memory>
#include <vector>

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

    Project& m_project;
    AudioEngine& m_engine;
    Settings& m_settings;

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
