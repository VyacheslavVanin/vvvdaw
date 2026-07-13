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
class PluginListWidget;
class QSplitter;
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
    void closeAllPluginWindows();
    void updateRulerSpacers(int panelWidth);
    void syncPluginListSplitters(int senderIndex);

    bool eventFilter(QObject* obj, QEvent* event) override;

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
    QWidget* m_rulerSpacer1 = nullptr;
    QWidget* m_rulerSpacer2 = nullptr;
    QWidget* m_scrollSpacer = nullptr;
    QWidget* m_busPanelGrip = nullptr;
    bool m_gripDragging = false;
    int m_gripStartY = 0;
    int m_gripStartHeight = 0;

    struct TrackRow {
        TrackPanelWidget* panel = nullptr;
        PluginListWidget* pluginList = nullptr;
        TrackViewWidget* view = nullptr;
        QSplitter* innerSplitter = nullptr;
    };
    std::vector<TrackRow> m_trackRows;

    std::vector<PluginWindow*> m_pluginWindows;
    std::vector<QSplitter*> m_trackSplitters;
    bool m_syncingSplitters = false;
    int m_savedPluginListWidth = 200;

    double m_zoom = vvvdaw::DefaultZoom;
    int64_t m_scrollOffset = 0;
    double m_snapResolution = 4.0;
};
