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
#include <unordered_map>

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
class InstrumentPanelWidget;
class PianoRollWindow;
class PluginWindow;
class PluginInstance;
class PluginChain;
class PluginListWidget;
class QSplitter;
class QVBoxLayout;
struct DeviceInfo;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Project& project, AudioEngine& engine, Settings& settings,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    friend class MainWindowTest;

    void setupUi();
    void setupMenus();
    void setupTransportConnections();
    void setupTimer();
    void loadStyleSheet();
    void rebuildTracks();
    // Stop playback, tear down the current project's UI and swap in a new one.
    void replaceProject(Project project);
    bool moveEventToTrack(int srcIdx, int dstIdx, int64_t eventId, int64_t newStartSample);
    void refreshBusCombos();
    void syncZoom();
    void syncScrollPositions(int value);
    void executeCommand(std::unique_ptr<class UndoCommand> cmd);
    void pushCommand(std::unique_ptr<class UndoCommand> cmd);
    void performUndo();
    void performRedo();
    void toggleSnapToGrid();
    void openPluginEditor(PluginInstance* plugin);
    void openPianoRoll(int trackIndex, int64_t eventId);
    void resyncPianoRollWindows();
    // Recompute the MIDI keyboard preview track and piano-roll recording hints
    // from the currently open piano roll windows.
    void updateMidiPreviewTarget();
    // Release held MIDI-keyboard preview notes and point further preview /
    // recording input at the given piano-roll event.
    void setActiveMidiPreview(int trackIndex, int64_t eventId);
    class PluginChain* findChainForPlugin(PluginInstance* plugin);
    void closeAllPluginWindows();
    void closePluginWindowsFor(const std::vector<PluginInstance*>& plugins);
    void closePluginWindowsFor(PluginInstance* plugin);
    // Push the playhead position into the engine, rulers and all track views.
    void syncPlayheadViews(int64_t sample);
    void updateRulerSpacers(int panelWidth);
    void syncPluginListSplitters(int senderIndex);

    // setupUi() steps
    void setupRulerConnections();
    void setupBusPanel(QVBoxLayout* layout);
    void setupInstrumentPanel(QVBoxLayout* layout);

    // rebuildTracks() steps
    void teardownTrackRows();
    void buildTrackRow(int trackIndex, bool odd,
                       const std::vector<DeviceInfo>& devices,
                       const std::vector<std::pair<int, QString>>& midiOutList,
                       const std::vector<QString>& instrumentNames);
    void syncAfterRebuild();
    void syncSnapUnit();

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
    InstrumentPanelWidget* m_instrumentPanel = nullptr;
    QWidget* m_rulerSpacer1 = nullptr;
    QWidget* m_rulerSpacer2 = nullptr;
    QWidget* m_scrollSpacer = nullptr;
    QWidget* m_busPanelGrip = nullptr;
    QWidget* m_instrumentPanelGrip = nullptr;
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
    std::vector<PianoRollWindow*> m_pianoRollWindows;
    std::vector<QSplitter*> m_trackSplitters;
    bool m_syncingSplitters = false;
    int m_savedPluginListWidth = 200;

    double m_zoom = vvvdaw::DefaultZoom;
    int64_t m_scrollOffset = 0;
    double m_snapResolution = 4.0;

    int m_midiPreviewTrack = -1;
    std::unordered_map<int, int64_t> m_midiTargetHints;
};
