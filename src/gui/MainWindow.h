#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <QScrollBar>
#include <memory>
#include <optional>
#include <vector>

#include "core/Constants.h"
#include "core/UndoStack.h"
#include "plugin/PluginManager.h"

class Project;
class AudioEngine;
class Settings;
class TransportPanel;
class TimelineRuler;
class MeasureRuler;
class TempoWidget;
class TrackPanelWidget;
class TrackViewWidget;
class BusPanelWidget;
class PluginWindow;
class PluginInstance;
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
    void setupTransportConnections();
    void setupTimer();
    void loadStyleSheet();
    void rebuildTracks();
    void refreshBusCombos();
    void syncZoom();
    void syncScrollPositions(int value);
    void pushUndoState();
    void performUndo();
    void performRedo();
    void applyState(const std::optional<QJsonObject>& state);
    void openPluginEditor(PluginInstance* plugin);

    Project& m_project;
    AudioEngine& m_engine;
    Settings& m_settings;
    UndoStack m_undoStack;
    PluginManager m_pluginManager;

    TransportPanel* m_transportPanel = nullptr;
    TempoWidget* m_tempoWidget = nullptr;
    TimelineRuler* m_timelineRuler = nullptr;
    MeasureRuler* m_measureRuler = nullptr;
    QScrollBar* m_horizontalScroll = nullptr;
    QWidget* m_trackContainer = nullptr;
    QVBoxLayout* m_trackLayout = nullptr;
    BusPanelWidget* m_busPanel = nullptr;

    struct TrackRow {
        TrackPanelWidget* panel = nullptr;
        TrackViewWidget* view = nullptr;
    };
    std::vector<TrackRow> m_trackRows;

    std::vector<PluginWindow*> m_pluginWindows;

    double m_zoom = vvvdaw::DefaultZoom;
    int64_t m_scrollOffset = 0;
    double m_snapResolution = 4.0;
};
