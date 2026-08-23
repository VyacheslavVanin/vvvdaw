#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransportPanel.h"
#include "TimelineRuler.h"
#include "MeasureRuler.h"
#include "TempoWidget.h"
#include "TrackPanelWidget.h"
#include "TrackViewWidget.h"
#include "BusPanelWidget.h"
#include "InstrumentPanelWidget.h"
#include "PianoRollWindow.h"
#include "PluginListWidget.h"
#include "PluginWindow.h"
#include "StartDialog.h"
#include "core/UndoStack.h"
#include "core/TimeUtils.h"
#include "commands/TrackCommands.h"
#include "commands/BusCommands.h"
#include "commands/ProjectCommands.h"
#include "commands/EventCommands.h"
#include "commands/MidiCommands.h"
#include "commands/InstrumentCommands.h"
#include "commands/PluginCommands.h"
#include "commands/SnapshotCommand.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "audio/AudioEngine.h"
#include "audio/DeviceInfo.h"
#include "core/Settings.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginChain.h"
#include "plugin/LV2Instance.h"

using vvvdaw::TransportState;
#include <QApplication>
#include <QMouseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QJsonArray>
#include <cmath>

MainWindow::MainWindow(Project& project, AudioEngine& engine, Settings& settings,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_engine(engine)
    , m_settings(settings)
{
    setWindowTitle("vvvdaw — " + m_project.name());
    resize(qBound(800, m_settings.mainWindowWidth, 16000),
           qBound(500, m_settings.mainWindowHeight, 9000));

    m_pluginManager.loadCache();
    m_pluginManager.scanDirectories(
        m_settings.pluginScanPaths.empty()
            ? PluginManager::defaultScanPaths()
            : m_settings.pluginScanPaths);
    m_project.setPluginManager(&m_pluginManager);

    setupUi();
    setupMenus();
    loadStyleSheet();

    m_engine.setProject(&m_project);
    m_project.setSampleRate(m_engine.sampleRate());
    m_engine.activateAllPlugins();
    rebuildTracks();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Transport row
    m_transportPanel = new TransportPanel(this);
    auto* transportRow = new QHBoxLayout;
    transportRow->setContentsMargins(0, 0, 0, 0);
    transportRow->addStretch();
    transportRow->addWidget(m_transportPanel, 0, Qt::AlignCenter);
    transportRow->addStretch();
    m_tempoWidget = new TempoWidget(this);
    transportRow->addWidget(m_tempoWidget, 0, Qt::AlignRight);
    layout->addLayout(transportRow);

    // Ruler row 1
    auto* rulerRow1 = new QHBoxLayout;
    rulerRow1->setContentsMargins(0, 0, 0, 0);
    rulerRow1->setSpacing(0);
    m_rulerSpacer1 = new QWidget(this);
    m_rulerSpacer1->setFixedWidth(400);
    m_rulerSpacer1->setStyleSheet("background-color: #2a2a2a;");
    m_timelineRuler = new TimelineRuler(this);
    rulerRow1->addWidget(m_rulerSpacer1);
    rulerRow1->addWidget(m_timelineRuler, 1);
    layout->addLayout(rulerRow1);

    // Ruler row 2
    auto* rulerRow2 = new QHBoxLayout;
    rulerRow2->setContentsMargins(0, 0, 0, 0);
    rulerRow2->setSpacing(0);
    m_rulerSpacer2 = new QWidget(this);
    m_rulerSpacer2->setFixedWidth(400);
    m_rulerSpacer2->setStyleSheet("background-color: #252525;");
    m_measureRuler = new MeasureRuler(this);
    rulerRow2->addWidget(m_rulerSpacer2);
    rulerRow2->addWidget(m_measureRuler, 1);
    layout->addLayout(rulerRow2);

    setupRulerConnections();
    // Track scroll area
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_trackContainer = new QWidget(scrollArea);
    m_trackContainer->setAutoFillBackground(true);
    QPalette containerPal = m_trackContainer->palette();
    containerPal.setColor(QPalette::Window, QColor("#2a2a2a"));
    m_trackContainer->setPalette(containerPal);
    scrollArea->viewport()->setAutoFillBackground(true);
    QPalette viewportPal = scrollArea->viewport()->palette();
    viewportPal.setColor(QPalette::Window, QColor("#2a2a2a"));
    scrollArea->viewport()->setPalette(viewportPal);
    m_trackLayout = new QVBoxLayout(m_trackContainer);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(2);
    scrollArea->setWidget(m_trackContainer);

    layout->addWidget(scrollArea, 1);

    m_horizontalScroll = new QScrollBar(Qt::Horizontal, this);
    m_horizontalScroll->setRange(0, 1000000);
    connect(m_horizontalScroll, &QScrollBar::valueChanged, this, &MainWindow::syncScrollPositions);

    auto* scrollRow = new QHBoxLayout;
    scrollRow->setContentsMargins(0, 0, 0, 0);
    scrollRow->setSpacing(0);
    m_scrollSpacer = new QWidget(this);
    m_scrollSpacer->setFixedWidth(400);
    m_scrollSpacer->setStyleSheet("background-color: #2a2a2a;");
    scrollRow->addWidget(m_scrollSpacer);
    scrollRow->addWidget(m_horizontalScroll);
    layout->addLayout(scrollRow);

    setupBusPanel(layout);
    setupInstrumentPanel(layout);
    setCentralWidget(central);

    setupTransportConnections();
    setupTimer();
}

void MainWindow::setupRulerConnections() {
    auto onPlayheadClicked = [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
        syncPlayheadViews(sample);
        m_transportPanel->setTimeText(TimeUtils::formatTime(sample, m_engine.sampleRate()));
    };
    connect(m_timelineRuler, &TimelineRuler::playheadClicked, this, onPlayheadClicked);
    connect(m_measureRuler, &MeasureRuler::playheadClicked, this, onPlayheadClicked);

    // Loop signals
    auto onLoopCreated = [this](int64_t start, int64_t end) {
        m_project.setLoop(start, end);
        m_timelineRuler->setLoop(start, end);
        m_measureRuler->setLoop(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::loopCreated, this, onLoopCreated);
    connect(m_measureRuler, &MeasureRuler::loopCreated, this, onLoopCreated);

    auto onLoopRemoved = [this] {
        m_project.clearLoop();
        m_timelineRuler->clearLoop();
        m_measureRuler->clearLoop();
    };
    connect(m_timelineRuler, &TimelineRuler::loopRemoved, this, onLoopRemoved);
    connect(m_measureRuler, &MeasureRuler::loopRemoved, this, onLoopRemoved);

    auto onLoopChanged = [this](int64_t start, int64_t end) {
        m_project.setLoop(start, end);
        m_timelineRuler->setLoop(start, end);
        m_measureRuler->setLoop(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::loopChanged, this, onLoopChanged);
    connect(m_measureRuler, &MeasureRuler::loopChanged, this, onLoopChanged);

    // Record region signals
    auto onRecordRegionCreated = [this](int64_t start, int64_t end) {
        m_project.setRecordRegion(start, end);
        m_timelineRuler->setRecordRegion(start, end);
        m_measureRuler->setRecordRegion(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::recordRegionCreated, this, onRecordRegionCreated);
    connect(m_measureRuler, &MeasureRuler::recordRegionCreated, this, onRecordRegionCreated);

    auto onRecordRegionRemoved = [this] {
        m_project.clearRecordRegion();
        m_timelineRuler->clearRecordRegion();
        m_measureRuler->clearRecordRegion();
    };
    connect(m_timelineRuler, &TimelineRuler::recordRegionRemoved, this, onRecordRegionRemoved);
    connect(m_measureRuler, &MeasureRuler::recordRegionRemoved, this, onRecordRegionRemoved);

    auto onRecordRegionChanged = [this](int64_t start, int64_t end) {
        m_project.setRecordRegion(start, end);
        m_timelineRuler->setRecordRegion(start, end);
        m_measureRuler->setRecordRegion(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::recordRegionChanged, this, onRecordRegionChanged);
    connect(m_measureRuler, &MeasureRuler::recordRegionChanged, this, onRecordRegionChanged);

    // Tempo widget signals
    auto updateSnapUnit = [this] { syncSnapUnit(); };
    connect(m_tempoWidget, &TempoWidget::tempoChanged, this, [this, updateSnapUnit](double bpm) {
        executeCommand(std::make_unique<SetTempoCommand>(m_project, m_project.tempo(), bpm));
        updateSnapUnit();
        if (m_engine.transportState() == TransportState::Playing)
            m_engine.setPlayPosition(m_engine.playPosition());
    });
    connect(m_tempoWidget, &TempoWidget::timeSignatureChanged, this, [this, updateSnapUnit](int num, int den) {
        executeCommand(std::make_unique<SetTimeSigCommand>(m_project, m_project.timeSigNum(), m_project.timeSigDen(), num, den));
        updateSnapUnit();
    });
    connect(m_tempoWidget, &TempoWidget::snapResolutionChanged, this, [this, updateSnapUnit](double resolution) {
        m_snapResolution = resolution;
        updateSnapUnit();
    });

    connect(m_tempoWidget, &TempoWidget::metronomeToggled, this, [this](bool on) {
        m_project.setMetronomeEnabled(on);
        m_engine.setMetronomeEnabled(on);
    });
    connect(m_tempoWidget, &TempoWidget::precountToggled, this, [this](bool on) {
        m_project.setPrecountEnabled(on);
        m_engine.setPrecountEnabled(on);
    });

}

void MainWindow::setupBusPanel(QVBoxLayout* layout) {
    // Bus panel grip (draggable resize handle)
    m_busPanelGrip = new QWidget(this);
    m_busPanelGrip->setFixedHeight(6);
    m_busPanelGrip->setCursor(Qt::SizeVerCursor);
    m_busPanelGrip->setStyleSheet("background-color: #555;:hover { background-color: #777; }");
    layout->addWidget(m_busPanelGrip);

    // Bus panel
    m_busPanel = new BusPanelWidget(m_project, this);
    m_busPanel->setPluginManager(&m_pluginManager);
    m_busPanel->setAudioEngine(&m_engine);
    m_busPanel->setAudioParams(m_engine.sampleRate(), m_engine.bufferSize());
    m_busPanel->setFixedHeight(qBound(80, m_settings.busPanelHeight, 600));
    m_busPanel->setVisible(m_settings.busPanelVisible);
    m_busPanelGrip->setVisible(m_settings.busPanelVisible);
    layout->addWidget(m_busPanel);
    if (m_settings.busPanelVisible)
        m_busPanel->rebuild();

    connect(m_busPanel, &BusPanelWidget::addBusRequested, this, [this] {
        executeCommand(std::make_unique<AddBusCommand>(m_project));
    });

    connect(m_busPanel, &BusPanelWidget::removeBusRequested, this, [this](int index) {
        if (index <= 0 || index >= static_cast<int>(m_project.buses().size()))
            return;
        if (!m_project.buses()[index].removable())
            return;
        std::vector<PluginInstance*> plugins;
        auto& chain = m_project.buses()[index].pluginChain();
        for (int j = 0; j < chain.count(); ++j)
            plugins.push_back(chain.plugin(j));
        closePluginWindowsFor(plugins);
        executeCommand(std::make_unique<RemoveBusCommand>(m_project, index));
    });

    connect(m_busPanel, &BusPanelWidget::busVolumeWillChange, this,
            [this](int busIndex, float oldVal, float newVal) {
        pushCommand(std::make_unique<SetBusVolumeCommand>(m_project, busIndex, oldVal, newVal));
    });
    connect(m_busPanel, &BusPanelWidget::busPanWillChange, this,
            [this](int busIndex, float oldVal, float newVal) {
        pushCommand(std::make_unique<SetBusPanCommand>(m_project, busIndex, oldVal, newVal));
    });
    connect(m_busPanel, &BusPanelWidget::busSoloWillChange, this,
            [this](int busIndex, bool oldVal, bool newVal) {
        pushCommand(std::make_unique<SetBusSoloCommand>(m_project, busIndex, oldVal, newVal));
    });
    connect(m_busPanel, &BusPanelWidget::busMuteWillChange, this,
            [this](int busIndex, bool oldVal, bool newVal) {
        pushCommand(std::make_unique<SetBusMuteCommand>(m_project, busIndex, oldVal, newVal));
    });
    connect(m_busPanel, &BusPanelWidget::busNameWillChange, this,
            [this](int busIndex, const QString& oldName, const QString& newName) {
        pushCommand(std::make_unique<SetBusNameCommand>(m_project, busIndex, oldName, newName));
        if (busIndex >= 0 && busIndex < static_cast<int>(m_project.buses().size()))
            m_project.buses()[busIndex].setName(newName);
        refreshBusCombos();
        m_busPanel->refreshOutCombos();
        m_instrumentPanel->refreshOutCombos();
    });
    connect(m_busPanel, &BusPanelWidget::busOutputWillChange, this,
            [this](int busIndex, int oldVal, int newVal) {
        pushCommand(std::make_unique<SetBusOutputCommand>(m_project, busIndex, oldVal, newVal));
    });

    connect(m_busPanel, &BusPanelWidget::busColorWillChange, this,
            [this](const BusPanelWidget::BusColorChange& change) {
        std::vector<SetBusColorCommand::Entry> entries;
        auto collect = [&](int idx) {
            const AudioBus* bus = m_project.busAt(idx);
            if (!bus) return;
            SetBusColorCommand::Entry e;
            e.busIndex = idx;
            e.oldColor = bus->color();
            e.oldSet = bus->colorSet();
            e.newColor = change.newColor;
            e.newSet = change.newSet;
            entries.push_back(std::move(e));
        };
        // Clearing the override on a descendant: drop its manual color so it
        // inherits from its (colored) ancestor.
        auto collectClear = [&](int idx) {
            const AudioBus* bus = m_project.busAt(idx);
            if (!bus || !bus->colorSet()) return;
            SetBusColorCommand::Entry e;
            e.busIndex = idx;
            e.oldColor = bus->color();
            e.oldSet = true;
            e.newSet = false;
            entries.push_back(std::move(e));
        };
        collect(change.busIndex);
        if (change.overrideChildren) {
            for (int c : m_project.folderDescendants(change.busIndex))
                collectClear(c);
        }
        if (entries.empty()) return;
        executeCommand(std::make_unique<SetBusColorCommand>(m_project, std::move(entries)));
        m_busPanel->refreshColors();
    });

    connect(m_busPanel, &BusPanelWidget::busSendAddRequested, this, [this](int busIndex) {
        if (busIndex < 0 || busIndex >= static_cast<int>(m_project.buses().size())) return;
        executeCommand(std::make_unique<AddBusSendCommand>(m_project, busIndex));
    });

    connect(m_busPanel, &BusPanelWidget::busSendRemoveRequested, this,
            [this](int busIndex, int sendIndex) {
        if (busIndex < 0 || busIndex >= static_cast<int>(m_project.buses().size())) return;
        if (sendIndex < 0 ||
            sendIndex >= static_cast<int>(m_project.buses()[busIndex].sends().size())) return;
        executeCommand(std::make_unique<RemoveBusSendCommand>(m_project, busIndex, sendIndex));
    });

    connect(m_busPanel, &BusPanelWidget::busSendTargetWillChange, this,
            [this](int busIndex, int sendIndex, int oldBus, int newBus) {
        pushCommand(std::make_unique<SetBusSendTargetCommand>(m_project, busIndex, sendIndex, oldBus, newBus));
    });

    connect(m_busPanel, &BusPanelWidget::busSendLevelWillChange, this,
            [this](int busIndex, int sendIndex, float oldLevel, float newLevel) {
        pushCommand(std::make_unique<SetBusSendLevelCommand>(m_project, busIndex, sendIndex, oldLevel, newLevel));
    });

    connect(m_busPanel, &BusPanelWidget::busSendPreWillChange, this,
            [this](int busIndex, int sendIndex, bool oldPre, bool newPre) {
        pushCommand(std::make_unique<SetBusSendPreCommand>(m_project, busIndex, sendIndex, oldPre, newPre));
    });

    connect(m_busPanel, &BusPanelWidget::busesMoved, this,
            [this](std::vector<int> oldOrder, std::vector<int> newOrder,
                   std::vector<std::pair<int, int>> oldParents,
                   std::vector<std::pair<int, int>> newParents) {
        executeCommand(std::make_unique<MoveBusesCommand>(
            m_project, std::move(oldOrder), std::move(newOrder),
            std::move(oldParents), std::move(newParents)));
    });

    connect(m_busPanel, &BusPanelWidget::busFolderCollapseWillChange, this,
            [this](int busIndex, bool oldVal, bool newVal) {
        pushCommand(std::make_unique<SetBusFolderCollapsedCommand>(
            m_project, busIndex, oldVal, newVal));
    });

    connect(m_busPanel, &BusPanelWidget::createBusFolderRequested, this,
            [this](const QString& name, const std::vector<int>& children) {
        if (name.isEmpty() || children.empty()) return;
        executeCommand(std::make_unique<CreateBusFolderCommand>(m_project, name, children));
        if (m_busPanel->isVisible())
            m_busPanel->rebuild();
    });

    connect(m_busPanel, &BusPanelWidget::openBusPluginEditorRequested, this,
            [this](int busIndex, PluginInstance* plugin) {
        Q_UNUSED(busIndex);
        openPluginEditor(plugin);
    });

    connect(m_busPanel, &BusPanelWidget::busPluginWillBeRemoved, this,
            [this](PluginInstance* plugin) {
        closePluginWindowsFor(plugin);
    });

    connect(m_busPanel, &BusPanelWidget::busPluginAddRequested, this,
            [this](int busIndex, const QString& type, const QString& path) {
        if (busIndex < 0 || busIndex >= static_cast<int>(m_project.buses().size())) return;
        QJsonObject pluginJson;
        pluginJson["type"] = type;
        pluginJson["path"] = path;
        auto cmd = std::make_unique<AddPluginCommand>(
            m_project.buses()[busIndex].pluginChain(), pluginJson, &m_pluginManager,
            static_cast<double>(m_engine.sampleRate()), m_engine.bufferSize());
        cmd->setBeforeRemoveCallback([this](PluginInstance* plugin) {
            closePluginWindowsFor(plugin);
        });
        executeCommand(std::move(cmd));
        if (auto* added = dynamic_cast<AddPluginCommand*>(m_undoStack.topCommand()))
            if (added->addedPlugin()) openPluginEditor(added->addedPlugin());
    });
    connect(m_busPanel, &BusPanelWidget::busPluginRemoved, this, [this](int, int) {
        pushCommand(std::make_unique<SnapshotCommand>(m_project));
    });
    connect(m_busPanel, &BusPanelWidget::busPluginWillBeMoved, this,
            [this](int busIndex, int from, int to) {
        if (busIndex >= 0 && busIndex < static_cast<int>(m_project.buses().size())) {
            pushCommand(std::make_unique<MovePluginCommand>(
                m_project.buses()[busIndex].pluginChain(), from, to));
        }
    });
    connect(m_busPanel, &BusPanelWidget::busPluginWillBeToggled, this, [this](int) {
        pushCommand(std::make_unique<SnapshotCommand>(m_project));
    });

    m_busPanelGrip->installEventFilter(this);
}

void MainWindow::setupInstrumentPanel(QVBoxLayout* layout) {
    // Instrument panel grip (draggable resize handle)
    m_instrumentPanelGrip = new QWidget(this);
    m_instrumentPanelGrip->setFixedHeight(6);
    m_instrumentPanelGrip->setCursor(Qt::SizeVerCursor);
    m_instrumentPanelGrip->setStyleSheet("background-color: #555;:hover { background-color: #777; }");
    layout->addWidget(m_instrumentPanelGrip);

    // Instrument panel
    m_instrumentPanel = new InstrumentPanelWidget(m_project, this);
    m_instrumentPanel->setPluginManager(&m_pluginManager);
    m_instrumentPanel->setAudioParams(m_engine.sampleRate(), m_engine.bufferSize());
    m_instrumentPanel->setFixedHeight(qBound(100, m_settings.instrumentPanelHeight, 600));
    m_instrumentPanel->setVisible(m_settings.instrumentPanelVisible);
    m_instrumentPanelGrip->setVisible(m_settings.instrumentPanelVisible);
    layout->addWidget(m_instrumentPanel);
    if (m_settings.instrumentPanelVisible)
        m_instrumentPanel->rebuild();

    connect(m_instrumentPanel, &InstrumentPanelWidget::addInstrumentRequested, this, [this] {
        executeCommand(std::make_unique<AddInstrumentCommand>(m_project));
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::removeInstrumentRequested, this,
            [this](int index) {
        if (index < 0 || index >= static_cast<int>(m_project.instruments().size()))
            return;
        auto& inst = m_project.instruments()[index];
        std::vector<PluginInstance*> plugins;
        if (inst.synth())
            plugins.push_back(inst.synth());
        for (int j = 0; j < inst.effects().count(); ++j)
            plugins.push_back(inst.effects().plugin(j));
        closePluginWindowsFor(plugins);
        executeCommand(std::make_unique<RemoveInstrumentCommand>(
            m_project, index, &m_pluginManager, m_engine.sampleRate(), m_engine.bufferSize()));
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::volumeWillChange, this,
            [this](int index, float oldVal, float newVal) {
        pushCommand(std::make_unique<SetInstrumentVolumeCommand>(m_project, index, oldVal, newVal));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::panWillChange, this,
            [this](int index, float oldVal, float newVal) {
        pushCommand(std::make_unique<SetInstrumentPanCommand>(m_project, index, oldVal, newVal));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::soloWillChange, this,
            [this](int index, bool oldVal, bool newVal) {
        pushCommand(std::make_unique<SetInstrumentSoloCommand>(m_project, index, oldVal, newVal));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::muteWillChange, this,
            [this](int index, bool oldVal, bool newVal) {
        pushCommand(std::make_unique<SetInstrumentMuteCommand>(m_project, index, oldVal, newVal));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::nameWillChange, this,
            [this](int index, const QString& oldName, const QString& newName) {
        pushCommand(std::make_unique<SetInstrumentNameCommand>(m_project, index, oldName, newName));
        if (index >= 0 && index < static_cast<int>(m_project.instruments().size()))
            m_project.instruments()[index].setName(newName);
        refreshBusCombos();
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::outputWillChange, this,
            [this](int index, int oldVal, int newVal) {
        pushCommand(std::make_unique<SetInstrumentOutputCommand>(m_project, index, oldVal, newVal));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::routingWillChange, this,
            [this](int index, const QJsonObject& oldRouting, const QJsonObject& newRouting) {
        pushCommand(std::make_unique<SetInstrumentRoutingCommand>(m_project, index,
                                                                  oldRouting, newRouting));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::channelBusesCreated, this,
            [this](int index, const QJsonArray& createdBuses,
                   const QJsonObject& oldRouting, const QJsonObject& newRouting) {
        pushCommand(std::make_unique<AddChannelBusesCommand>(
            m_project, index, createdBuses, oldRouting, newRouting));
        rebuildTracks();
        refreshBusCombos();
        resyncPianoRollWindows();
        m_engine.refreshMidiOutputs();
        if (m_instrumentPanel->isVisible())
            m_instrumentPanel->rebuild();
        if (m_busPanel->isVisible())
            m_busPanel->rebuild();
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::synthAddRequested, this,
            [this](int index, const QString& type, const QString& path) {
        if (index < 0 || index >= static_cast<int>(m_project.instruments().size())) return;
        // Capture the old synth's state for undo before it is destroyed by the replace.
        QJsonObject oldSynthJson;
        if (auto* oldSynth = m_project.instruments()[index].synth()) {
            oldSynthJson = oldSynth->stateToJson();
            // Close the old synth's editor before it is destroyed by the replace.
            closePluginWindowsFor({oldSynth});
        }
        QJsonObject synthJson;
        synthJson["type"] = type;
        synthJson["path"] = path;
        auto cmd = std::make_unique<SetInstrumentSynthCommand>(
            m_project, index, oldSynthJson, synthJson, &m_pluginManager,
            m_engine.sampleRate(), m_engine.bufferSize());
        executeCommand(std::move(cmd));
        resyncPianoRollWindows();
        if (m_instrumentPanel->isVisible())
            m_instrumentPanel->rebuild();
        if (auto* synth = m_project.instruments()[index].synth())
            openPluginEditor(synth);
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::synthRemoveRequested, this,
            [this](int index) {
        if (index < 0 || index >= static_cast<int>(m_project.instruments().size())) return;
        auto& inst = m_project.instruments()[index];
        if (!inst.synth()) return;
        closePluginWindowsFor(inst.synth());
        auto cmd = std::make_unique<SetInstrumentSynthCommand>(
            m_project, index, inst.synth()->stateToJson(), QJsonObject(), &m_pluginManager,
            m_engine.sampleRate(), m_engine.bufferSize());
        executeCommand(std::move(cmd));
        resyncPianoRollWindows();
        if (m_instrumentPanel->isVisible())
            m_instrumentPanel->rebuild();
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::openSynthEditorRequested, this,
            [this](int index, PluginInstance* plugin) {
        Q_UNUSED(index);
        openPluginEditor(plugin);
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::openFxEditorRequested, this,
            [this](int, PluginInstance* plugin) { openPluginEditor(plugin); });

    connect(m_instrumentPanel, &InstrumentPanelWidget::pluginWillBeRemoved, this,
            [this](PluginInstance* plugin) {
        closePluginWindowsFor(plugin);
    });

    connect(m_instrumentPanel, &InstrumentPanelWidget::fxAddRequested, this,
            [this](int index, const QString& type, const QString& path) {
        if (index < 0 || index >= static_cast<int>(m_project.instruments().size())) return;
        QJsonObject pluginJson;
        pluginJson["type"] = type;
        pluginJson["path"] = path;
        auto cmd = std::make_unique<AddPluginCommand>(
            m_project.instruments()[index].effects(), pluginJson, &m_pluginManager,
            static_cast<double>(m_engine.sampleRate()), m_engine.bufferSize());
        cmd->setBeforeRemoveCallback([this](PluginInstance* plugin) {
            closePluginWindowsFor(plugin);
        });
        executeCommand(std::move(cmd));
        if (auto* added = dynamic_cast<AddPluginCommand*>(m_undoStack.topCommand()))
            if (added->addedPlugin()) openPluginEditor(added->addedPlugin());
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::pluginRemoved, this, [this](int, int) {
        pushCommand(std::make_unique<SnapshotCommand>(m_project));
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::pluginWillBeMoved, this,
            [this](int index, int from, int to) {
        if (index >= 0 && index < static_cast<int>(m_project.instruments().size())) {
            pushCommand(std::make_unique<MovePluginCommand>(
                m_project.instruments()[index].effects(), from, to));
        }
    });
    connect(m_instrumentPanel, &InstrumentPanelWidget::pluginWillBeToggled, this, [this](int) {
        pushCommand(std::make_unique<SnapshotCommand>(m_project));
    });

    m_instrumentPanelGrip->installEventFilter(this);


}

void MainWindow::setupTransportConnections() {
    connect(m_transportPanel, &TransportPanel::backClicked, this, [this] {
        m_engine.setPlayPosition(0);
        m_scrollOffset = 0;
        syncScrollPositions(0);
    });

    auto refreshTrackViews = [this] {
        for (auto& row : m_trackRows)
            row.view->update();
    };

    connect(m_transportPanel, &TransportPanel::playClicked, this, [this, refreshTrackViews] {
        if (m_engine.transportState() == TransportState::Paused) {
            m_engine.setTransportState(TransportState::Playing);
        } else if (m_engine.transportState() != TransportState::Playing) {
            m_engine.setTransportState(TransportState::Playing);
        }
        m_transportPanel->setPlaying(true);
        refreshTrackViews();
    });

    connect(m_transportPanel, &TransportPanel::pauseClicked, this, [this, refreshTrackViews] {
        m_engine.setTransportState(TransportState::Paused);
        m_transportPanel->setPlaying(false);
        refreshTrackViews();
    });

    connect(m_transportPanel, &TransportPanel::stopClicked, this, [this, refreshTrackViews] {
        m_engine.setTransportState(TransportState::Stopped);
        m_engine.setPlayPosition(0);
        syncPlayheadViews(0);
        m_transportPanel->setPlaying(false);
        m_transportPanel->setRecording(false);
        refreshTrackViews();
    });

    auto toggleRecording = [this, refreshTrackViews] {
        TransportState s = m_engine.transportState();
        if (s == TransportState::Recording) {
            m_engine.setTransportState(TransportState::Stopped);
            m_transportPanel->setRecording(false);
            refreshTrackViews();
        } else {
            m_engine.setTransportState(TransportState::Recording);
            m_transportPanel->setRecording(true);
        }
        syncRecordingPreviews();
    };
    connect(m_transportPanel, &TransportPanel::recordClicked, this, toggleRecording);

    auto* recordShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    connect(recordShortcut, &QShortcut::activated, this, toggleRecording);

    auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, [this, refreshTrackViews] {
        TransportState s = m_engine.transportState();
        if (s == TransportState::Playing || s == TransportState::Recording) {
            m_engine.setTransportState(TransportState::Paused);
            m_transportPanel->setPlaying(false);
        } else {
            if (s == TransportState::Stopped) {
                m_scrollOffset = 0;
                syncScrollPositions(0);
            }
            m_engine.setTransportState(TransportState::Playing);
            m_transportPanel->setPlaying(true);
        }
        refreshTrackViews();
    });

    auto* snapShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    connect(snapShortcut, &QShortcut::activated, this, &MainWindow::toggleSnapToGrid);

    connect(m_transportPanel, &TransportPanel::forwardClicked, this, [this] {
        int64_t maxEnd = 0;
        for (const auto& track : m_project.tracks()) {
            for (const auto& event : track.events()) {
                int64_t end = event.startSample() + event.durationSample();
                if (end > maxEnd) maxEnd = end;
            }
        }
        m_engine.setPlayPosition(maxEnd > 0 ? maxEnd : 48000 * 30);
    });

    connect(m_transportPanel, &TransportPanel::snapToggled, this, [this](bool snap) {
        m_project.setSnapToGrid(snap);
        for (auto& row : m_trackRows) {
            if (row.view)
                row.view->setSnapToGrid(snap);
        }
        m_timelineRuler->setSnapToGrid(snap);
        m_measureRuler->setSnapToGrid(snap);
    });
}

void MainWindow::setupTimer() {
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        TransportState s = m_engine.transportState();
        int64_t pos = m_engine.playPosition();
        m_transportPanel->setTimeText(TimeUtils::formatTime(pos, m_engine.sampleRate()));

        if (s == TransportState::Playing || s == TransportState::Recording) {
            int64_t viewWidth = m_trackContainer->width();
            double pixelPos = pos * m_zoom;
            double viewEnd = m_scrollOffset * m_zoom + viewWidth * 0.7;
            if (pixelPos > viewEnd) {
                m_scrollOffset = pos - static_cast<int64_t>(viewWidth * 0.3 / m_zoom);
                if (m_scrollOffset < 0) m_scrollOffset = 0;
                syncScrollPositions(static_cast<int>(m_scrollOffset / vvvdaw::ScrollStepSamples));
            }
        }

        syncPlayheadViews(pos);
        for (auto* pr : m_pianoRollWindows)
            pr->setPlayheadSample(pos);
        syncRecordingPreviews();

        // MIDI transport control (CC / note mapping from the input device).
        auto commands = m_engine.takeMidiTransportCommands();
        if (!commands.empty()) {
            for (MidiTransportCommand cmd : commands) {
                switch (cmd) {
                case MidiTransportCommand::Play: {
                    TransportState st = m_engine.transportState();
                    if (st == TransportState::Recording) {
                        m_engine.setTransportState(TransportState::Stopped);
                        m_transportPanel->setRecording(false);
                    }
                    m_engine.setTransportState(TransportState::Playing);
                    m_transportPanel->setPlaying(true);
                    break;
                }
                case MidiTransportCommand::Stop:
                    m_engine.setTransportState(TransportState::Stopped);
                    m_engine.setPlayPosition(0);
                    syncPlayheadViews(0);
                    m_transportPanel->setPlaying(false);
                    m_transportPanel->setRecording(false);
                    break;
                case MidiTransportCommand::Record: {
                    TransportState st = m_engine.transportState();
                    if (st == TransportState::Recording) {
                        m_engine.setTransportState(TransportState::Stopped);
                        m_transportPanel->setRecording(false);
                    } else {
                        m_engine.setTransportState(TransportState::Recording);
                        m_transportPanel->setRecording(true);
                    }
                    break;
                }
                default:
                    break;
                }
            }
            for (auto& row : m_trackRows)
                row.view->update();
        }

        // MIDI recording: apply captured notes to the armed tracks' clips.
        bool recording = m_engine.transportState() == TransportState::Recording;
        bool midiChanged = m_engine.midiRecorder().pump(
            m_project, recording, m_engine.playPosition(), m_engine.midiRecordStartSample());
        if (midiChanged) {
            resyncPianoRollWindows();
            for (auto& row : m_trackRows)
                row.view->update();
        }
    });
    timer->start(40);
}

void MainWindow::executeCommand(std::unique_ptr<UndoCommand> cmd) {
    {
        // The audio thread reads the project under a shared lock; hold the
        // write lock so a command that destroys tracks/buses/plugin chains
        // cannot race a running callback.
        auto lock = m_project.writeLock();
        m_undoStack.execute(std::move(cmd));
    }
    rebuildTracks();
    refreshBusCombos();
    resyncPianoRollWindows();
    m_engine.refreshMidiOutputs();
    if (m_instrumentPanel->isVisible())
        m_instrumentPanel->rebuild();
}

void MainWindow::pushCommand(std::unique_ptr<UndoCommand> cmd) {
    m_undoStack.push(std::move(cmd));
}

void MainWindow::performUndo() {
    auto* cmd = m_undoStack.topCommand();
    if (!cmd || cmd->requiresPluginWindowsClose())
        closeAllPluginWindows();
    bool done = false;
    {
        auto lock = m_project.writeLock();
        done = m_undoStack.undo();
    }
    if (done) {
        rebuildTracks();
        refreshBusCombos();
        resyncPianoRollWindows();
        m_engine.refreshMidiOutputs();
        if (m_instrumentPanel->isVisible())
            m_instrumentPanel->rebuild();
        if (m_busPanel->isVisible())
            m_busPanel->rebuild();
    }
}

void MainWindow::performRedo() {
    auto* cmd = m_undoStack.topCommand();
    if (!cmd || cmd->requiresPluginWindowsClose())
        closeAllPluginWindows();
    bool done = false;
    {
        auto lock = m_project.writeLock();
        done = m_undoStack.redo();
    }
    if (done) {
        rebuildTracks();
        refreshBusCombos();
        resyncPianoRollWindows();
        m_engine.refreshMidiOutputs();
        if (m_instrumentPanel->isVisible())
            m_instrumentPanel->rebuild();
        if (m_busPanel->isVisible())
            m_busPanel->rebuild();
    }
}

void MainWindow::toggleSnapToGrid() {
    // Route through the transport panel so the button state, the project
    // flag, track views and rulers all stay in sync.
    m_transportPanel->setSnapToGrid(!m_project.snapToGrid());
}

void MainWindow::closeAllPluginWindows() {
    std::vector<PluginWindow*> toClose = m_pluginWindows;
    for (auto* w : toClose)
        w->close();
}

void MainWindow::closePluginWindowsFor(const std::vector<PluginInstance*>& plugins) {
    std::vector<PluginWindow*> toClose;
    for (auto* w : m_pluginWindows) {
        for (auto* p : plugins) {
            if (w->plugin() == p) {
                toClose.push_back(w);
                break;
            }
        }
    }
    for (auto* w : toClose)
        w->close();
}

void MainWindow::closePluginWindowsFor(PluginInstance* plugin) {
    closePluginWindowsFor(std::vector<PluginInstance*>{plugin});
}

void MainWindow::syncPlayheadViews(int64_t sample) {
    m_timelineRuler->setPlayheadPosition(sample);
    m_measureRuler->setPlayheadPosition(sample);
    for (auto& row : m_trackRows)
        row.view->setPlayheadPosition(sample);
}

void MainWindow::syncRecordingPreviews() {
    bool recording = m_engine.transportState() == TransportState::Recording;
    int64_t start = m_engine.midiRecordStartSample();
    int64_t end = m_project.hasRecordRegion() ? m_project.recordRegionEnd() : -1;

    // Refresh the live waveform peaks roughly every half second (12 ticks).
    constexpr int kPeakRefreshTicks = 12;
    bool refreshPeaks = false;
    if (recording) {
        refreshPeaks = (m_recordingPreviewTick == 0 || m_recordingPreviewTick % kPeakRefreshTicks == 0);
        ++m_recordingPreviewTick;
    } else {
        m_recordingPreviewTick = 0;
    }

    for (size_t i = 0; i < m_trackRows.size(); ++i) {
        TrackViewWidget* view = m_trackRows[i].view;
        if (!view || !view->track())
            continue;
        Track* track = view->track();
        bool active = recording && track->isRecordArmed() && track->type() == Track::Type::Audio;

        if (active && refreshPeaks) {
            std::vector<AudioClip::Peak> peaks;
            int64_t recordedFrames = 0;
            if (m_engine.recordingManager().copyRecordPeaks(static_cast<int>(i), peaks, recordedFrames))
                view->setRecordingPeaks(std::move(peaks), AudioClip::PEAK_STEP_FRAMES, recordedFrames);
            else
                view->setRecordingPeaks({}, 0, 0);
        } else if (!recording) {
            view->setRecordingPeaks({}, 0, 0);
        }

        view->setRecordingPreview(active, start, end);
    }
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Project", QKeySequence::New);
    auto* openAction = fileMenu->addAction("&Open Project...", QKeySequence::Open);
    auto* saveAction = fileMenu->addAction("&Save Project", QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"));
    auto* saveTemplateAction = fileMenu->addAction("Save as &Template...");
    fileMenu->addSeparator();
    auto* settingsAction = fileMenu->addAction("&Settings...");
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("&Quit", QKeySequence::Quit);

    connect(newAction, &QAction::triggered, this, [this] {
        // Reuse the start dialog so a new project can come from a template or
        // a recently opened project.
        StartDialog dialog(m_settings, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        switch (dialog.choice().action) {
        case StartDialog::Action::OpenRecent:
        case StartDialog::Action::Browse: {
            const QString path = dialog.choice().path;
            if (path.isEmpty()) return;
            Project project;
            if (!project.load(path)) {
                QMessageBox::warning(this, "Error", "Failed to load project.");
                m_settings.removeRecentProject(path);
                return;
            }
            replaceProject(std::move(project));
            m_settings.addRecentProject(path);
            setWindowTitle("vvvdaw - " + QFileInfo(path).absolutePath());
            break;
        }
        case StartDialog::Action::OpenTemplate: {
            Project project;
            if (!TemplateStore::loadTemplate(project, dialog.choice().templateName)) {
                QMessageBox::warning(this, "Error", "Failed to open template.");
                return;
            }
            replaceProject(std::move(project));
            setWindowTitle("vvvdaw - " + m_project.name());
            break;
        }
        case StartDialog::Action::Exit:
        default:
            break; // Cancel: keep the current project as-is.
        }
    });

    connect(openAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Open Project",
            QString(), "Project Files (project.json)");
        if (path.isEmpty()) return;

        Project project;
        if (!project.load(path)) {
            QMessageBox::warning(this, "Error", "Failed to load project.");
            return;
        }
        replaceProject(std::move(project));
        m_settings.addRecentProject(path);
        setWindowTitle("vvvdaw - " + QFileInfo(path).absolutePath());
    });

    connect(saveAction, &QAction::triggered, this, [this, saveAsAction] {
        if (m_project.filePath().isEmpty()) {
            saveAsAction->trigger();
            return;
        }
        if (!m_project.save(m_project.filePath())) {
            QMessageBox::warning(this, "Error", "Failed to save project.");
        } else {
            m_settings.addRecentProject(m_project.filePath());
        }
    });

    connect(saveAsAction, &QAction::triggered, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Choose Project Directory");
        if (dir.isEmpty()) return;

        QString path = dir + "/project.json";
        if (!m_project.save(path)) {
            QMessageBox::warning(this, "Error", "Failed to save project.");
            return;
        }
        m_settings.addRecentProject(path);
        setWindowTitle("vvvdaw - " + dir);
    });

    connect(saveTemplateAction, &QAction::triggered, this, [this] {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Save as Template",
                                             "Template name:", QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        name = TemplateStore::sanitizeName(name);
        if (name.isEmpty()) {
            QMessageBox::warning(this, "Save as Template",
                                 "The template name is not valid.");
            return;
        }
        bool overwrite = false;
        if (TemplateStore::exists(name)) {
            auto ret = QMessageBox::question(
                this, "Overwrite Template",
                QString("Template \"%1\" already exists.\nOverwrite it?").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
                return;
            overwrite = true;
        }
        if (!TemplateStore::saveTemplate(m_project, name, overwrite)) {
            QMessageBox::warning(this, "Save as Template",
                                 "Failed to save template.");
        }
    });

    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* undoAction = editMenu->addAction("&Undo", QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &MainWindow::performUndo);
    auto* redoAction = editMenu->addAction("&Redo", QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::performRedo);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* resetZoomAction = viewMenu->addAction("Reset &Zoom", QKeySequence("Ctrl+0"));
    connect(resetZoomAction, &QAction::triggered, this, [this] {
        m_zoom = vvvdaw::DefaultZoom;
        syncZoom();
        m_trackContainer->update();
    });

    auto* toggleBusPanelAction = viewMenu->addAction("Show &Bus Panel", QKeySequence("Ctrl+B"));
    toggleBusPanelAction->setCheckable(true);
    toggleBusPanelAction->setChecked(m_settings.busPanelVisible);
    connect(toggleBusPanelAction, &QAction::triggered, this, [this](bool checked) {
        m_settings.busPanelVisible = checked;
        m_busPanel->setVisible(checked);
        m_busPanelGrip->setVisible(checked);
        if (checked)
            m_busPanel->rebuild();
    });

    auto* toggleInstrumentPanelAction = viewMenu->addAction("Show &Instrument Panel", QKeySequence("Ctrl+I"));
    toggleInstrumentPanelAction->setCheckable(true);
    toggleInstrumentPanelAction->setChecked(m_settings.instrumentPanelVisible);
    connect(toggleInstrumentPanelAction, &QAction::triggered, this, [this](bool checked) {
        m_settings.instrumentPanelVisible = checked;
        m_instrumentPanel->setVisible(checked);
        m_instrumentPanelGrip->setVisible(checked);
        if (checked)
            m_instrumentPanel->rebuild();
    });

    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(m_settings, m_engine, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_engine.shutdown();
            m_engine.init(m_settings);
            m_engine.startStream();
            m_project.setSampleRate(m_engine.sampleRate());
        }
    });

    auto* trackMenu = menuBar()->addMenu("&Track");
    auto* addMonoAction = trackMenu->addAction("Add &Mono Track", QKeySequence("Ctrl+T"));
    connect(addMonoAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), 1));
    });
    auto* addStereoAction = trackMenu->addAction("Add &Stereo Track", QKeySequence("Ctrl+M"));
    connect(addStereoAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), 2));
    });
    auto* addMidiAction = trackMenu->addAction("Add &MIDI Track", QKeySequence("Ctrl+Shift+T"));
    connect(addMidiAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), Track::Type::Midi));
    });

    auto* deleteTrackAction = trackMenu->addAction("&Delete Track");
    connect(deleteTrackAction, &QAction::triggered, this, [this] {
        for (size_t i = 0; i < m_project.tracks().size(); ++i) {
            if (m_trackRows[i].panel->hasFocus() || m_trackRows[i].view->hasFocus()) {
                int idx = static_cast<int>(i);
                std::vector<PluginInstance*> plugins;
                auto& chain = m_project.tracks()[idx].pluginChain();
                for (int j = 0; j < chain.count(); ++j)
                    plugins.push_back(chain.plugin(j));
                closePluginWindowsFor(plugins);
                executeCommand(std::make_unique<RemoveTrackCommand>(m_project, idx, &m_pluginManager));
                return;
            }
        }
        for (size_t i = m_project.tracks().size(); i > 0; --i) {
            if (!m_trackRows[i - 1].view->selectedEventIds().empty()) {
                int idx = static_cast<int>(i - 1);
                std::vector<PluginInstance*> plugins;
                auto& chain = m_project.tracks()[idx].pluginChain();
                for (int j = 0; j < chain.count(); ++j)
                    plugins.push_back(chain.plugin(j));
                closePluginWindowsFor(plugins);
                executeCommand(std::make_unique<RemoveTrackCommand>(m_project, idx, &m_pluginManager));
                return;
            }
        }
    });

    trackMenu->addSeparator();

    auto* deleteAction = trackMenu->addAction("&Delete Event");
    connect(deleteAction, &QAction::triggered, this, [this] {
        for (auto& row : m_trackRows) {
            if (row.view->hasFocus()) {
                row.view->deleteSelectedEvent();
                return;
            }
        }
        for (auto& row : m_trackRows) {
            if (!row.view->selectedEventIds().empty()) {
                row.view->deleteSelectedEvent();
                return;
            }
        }
    });
}

void MainWindow::loadStyleSheet() {
    QFile qss(":/resources/style.qss");
    if (qss.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
}

void MainWindow::refreshBusCombos() {
    auto devices = AudioEngine::enumerateInputDevices();
    auto midiDevices = AudioEngine::enumerateMidiOutputDevices();
    std::vector<std::pair<int, QString>> midiOutList;
    for (const auto& dev : midiDevices)
        midiOutList.emplace_back(dev.id, dev.name);
    std::vector<QString> instrumentNames;
    for (const auto& inst : m_project.instruments())
        instrumentNames.push_back(inst.name());
    for (auto& row : m_trackRows) {
        if (row.panel) {
            row.panel->updateBusList(m_project.buses());
            row.panel->updateInputDeviceList(devices);
            row.panel->updateMidiOutputs(midiOutList, instrumentNames);
        }
    }
}

void MainWindow::replaceProject(Project project) {
    m_engine.setTransportState(TransportState::Stopped);
    m_engine.setProject(nullptr);
    closeAllPluginWindows();
    resyncPianoRollWindows();
    m_undoStack.clear();
    m_project = std::move(project);
    m_project.setPluginManager(&m_pluginManager);
    m_engine.setProject(&m_project);
    m_engine.activateAllPlugins();
    m_engine.refreshMidiOutputs();
    m_project.setSampleRate(m_engine.sampleRate());
    m_scrollOffset = 0;
    rebuildTracks();
}

void MainWindow::rebuildTracks() {
    teardownTrackRows();

    auto devices = AudioEngine::enumerateInputDevices();
    auto midiDevices = AudioEngine::enumerateMidiOutputDevices();
    std::vector<std::pair<int, QString>> midiOutList;
    for (const auto& dev : midiDevices)
        midiOutList.emplace_back(dev.id, dev.name);
    std::vector<QString> instrumentNames;
    for (const auto& inst : m_project.instruments())
        instrumentNames.push_back(inst.name());

    for (int i = 0; i < static_cast<int>(m_project.tracks().size()); ++i) {
        bool odd = (i % 2) != 0;
        buildTrackRow(i, odd, devices, midiOutList, instrumentNames);
    }

    syncAfterRebuild();
}

bool MainWindow::moveEventToTrack(int srcIdx, int dstIdx, int64_t eventId, int64_t newStartSample) {
    if (srcIdx == dstIdx || srcIdx < 0 || dstIdx < 0
        || srcIdx >= static_cast<int>(m_project.tracks().size())
        || dstIdx >= static_cast<int>(m_project.tracks().size()))
        return false;

    Track& src = m_project.tracks()[srcIdx];
    Track& dst = m_project.tracks()[dstIdx];
    if (src.type() != dst.type())
        return false;

    {
        // The move removes from and adds to the event vectors the audio thread
        // iterates; hold the write lock for the mutation.
        auto lock = m_project.writeLock();
        if (src.type() == Track::Type::Midi) {
            MidiEvent* ev = src.findMidiEvent(eventId);
            if (!ev) return false;
            ev->setStartSample(newStartSample);
            dst.importMidiEvent(*ev);
            src.removeMidiEvent(eventId);
        } else {
            AudioEvent* ev = src.findEvent(eventId);
            if (!ev) return false;
            ev->setStartSample(newStartSample);
            dst.importEvent(*ev);
            src.removeEvent(eventId);
        }
    }

    m_trackRows[srcIdx].view->updateFromTrack();
    m_trackRows[dstIdx].view->updateFromTrack();
    return true;
}

void MainWindow::teardownTrackRows() {
    if (!m_trackSplitters.empty())
        m_savedPluginListWidth = m_trackSplitters.front()->sizes().value(0, 200);

    for (auto& row : m_trackRows) {
        if (row.panel) {
            row.panel->hide();
            row.panel->deleteLater();
        }
        if (row.pluginList) {
            row.pluginList->hide();
            row.pluginList->deleteLater();
        }
        if (row.view) {
            row.view->hide();
            row.view->deleteLater();
        }
        if (row.innerSplitter) {
            row.innerSplitter->hide();
            row.innerSplitter->deleteLater();
        }
    }
    m_trackRows.clear();
    m_trackSplitters.clear();

    while (auto* item = m_trackLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

}

void MainWindow::buildTrackRow(int trackIndex, bool odd,
                               const std::vector<DeviceInfo>& devices,
                               const std::vector<std::pair<int, QString>>& midiOutList,
                               const std::vector<QString>& instrumentNames) {
    Track& track = m_project.tracks()[trackIndex];
    TrackRow row;
        row.panel = new TrackPanelWidget(&track, m_trackContainer);
        row.panel->setAlternateRow(odd);
        row.panel->updateBusList(m_project.buses());
        row.panel->updateInputDeviceList(devices);
        row.panel->updateMidiOutputs(midiOutList, instrumentNames);
        row.panel->updateFromTrack();

        row.pluginList = new PluginListWidget(m_trackContainer);
        row.pluginList->setTrack(&track);
        row.pluginList->setHeaderLabel("Effects:");
        row.pluginList->setPluginManager(&m_pluginManager);
        row.pluginList->setAudioParams(m_engine.sampleRate(), m_engine.bufferSize());
        row.pluginList->rebuild();

        row.view = new TrackViewWidget(&track, &m_project, m_trackContainer);
        row.view->setAlternateRow(odd);
        row.view->setZoom(m_zoom);
        row.view->setScrollOffset(m_scrollOffset);
        row.view->setSnapToGrid(m_project.snapToGrid());

        connect(row.view, &TrackViewWidget::zoomChanged, this, [this](double zoom) {
            m_zoom = zoom;
            syncZoom();
        });

        connect(row.panel, &TrackPanelWidget::addTrackRequested, this, [this](int channels) {
            executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), channels));
        });

        connect(row.panel, &TrackPanelWidget::addMidiTrackRequested, this, [this] {
            executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), Track::Type::Midi));
        });

        connect(row.panel, &TrackPanelWidget::addMidiEventRequested, this,
                [this, idx = trackIndex] {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size()))
                return;
            int64_t start = m_engine.playPosition();
            double snapUnit = m_project.samplesPerBar() / m_snapResolution;
            start = TimeUtils::snapSample(start, snapUnit);
            if (start < 0) start = 0;
            QJsonObject clipJson;
            clipJson["ppq"] = MidiClip::kPPQ;
            clipJson["lengthTicks"] = static_cast<qint64>(MidiClip::kPPQ) * 4;
            QJsonObject eventJson;
            eventJson["startSample"] = static_cast<qint64>(start);
            eventJson["offsetSample"] = 0;
            eventJson["durationSample"] = static_cast<qint64>(m_project.samplesPerBar());
            eventJson["clip"] = clipJson;
            auto cmd = std::make_unique<AddMidiEventCommand>(m_project, idx, eventJson);
            executeCommand(std::move(cmd));
            if (auto* added = dynamic_cast<AddMidiEventCommand*>(m_undoStack.topCommand()))
                if (added->createdEventId() >= 0)
                    openPianoRoll(idx, added->createdEventId());
        });

        connect(row.panel, &TrackPanelWidget::midiOutputChanged, this,
                [this, idx = trackIndex]
                (int deviceId, const QString& deviceName, int instrumentIndex) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) return;
            auto& trk = m_project.tracks()[idx];
            SetTrackMidiOutputCommand::Routing oldR;
            oldR.deviceId = trk.midiOutputDeviceId();
            oldR.deviceName = trk.midiOutputDeviceName();
            oldR.instrumentIndex = trk.instrumentIndex();
            SetTrackMidiOutputCommand::Routing newR;
            newR.deviceId = deviceId;
            newR.deviceName = deviceName;
            newR.instrumentIndex = instrumentIndex;
            if (oldR.deviceId == newR.deviceId && oldR.instrumentIndex == newR.instrumentIndex)
                return;
            pushCommand(std::make_unique<SetTrackMidiOutputCommand>(m_project, idx, oldR, newR));
            m_engine.refreshMidiOutputs();
        });

        connect(row.panel, &TrackPanelWidget::deleteRequested, this, [this, idx = trackIndex] {
            if (idx < static_cast<int>(m_project.tracks().size())) {
                std::vector<PluginInstance*> plugins;
                auto& chain = m_project.tracks()[idx].pluginChain();
                for (int j = 0; j < chain.count(); ++j)
                    plugins.push_back(chain.plugin(j));
                closePluginWindowsFor(plugins);
                executeCommand(std::make_unique<RemoveTrackCommand>(m_project, idx, &m_pluginManager));
            }
        });

        connect(row.panel, &TrackPanelWidget::beforeModify, this, [this] {
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });

        connect(row.panel, &TrackPanelWidget::armToggled, this,
                [this, idx = trackIndex](bool oldValue, bool newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackArmCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::soloToggled, this,
                [this, idx = trackIndex](bool oldValue, bool newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackSoloCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::muteToggled, this,
                [this, idx = trackIndex](bool oldValue, bool newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackMuteCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::monitorToggled, this,
                [this, idx = trackIndex](bool oldValue, bool newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackMonitorCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::panChanged, this,
                [this, idx = trackIndex](float oldValue, float newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackPanCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::volumeChanged, this,
                [this, idx = trackIndex](float oldValue, float newValue) {
            if (oldValue == newValue) return;
            pushCommand(std::make_unique<SetTrackVolumeCommand>(m_project, idx, oldValue, newValue));
        });

        connect(row.panel, &TrackPanelWidget::outputBusChanged, this,
                [this, idx = trackIndex](int oldIndex, int newIndex) {
            if (oldIndex == newIndex) return;
            pushCommand(std::make_unique<SetTrackOutputCommand>(m_project, idx, oldIndex, newIndex));
        });

        connect(row.pluginList, &PluginListWidget::openEditorRequested, this,
                &MainWindow::openPluginEditor);

        connect(row.pluginList, &PluginListWidget::pluginWillBeRemoved, this,
                [this](PluginInstance* plugin) {
            closePluginWindowsFor(plugin);
        });

        connect(row.pluginList, &PluginListWidget::pluginAddRequested, this,
                [this, idx = trackIndex](const QString& type, const QString& path) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) return;
            QJsonObject pluginJson;
            pluginJson["type"] = type;
            pluginJson["path"] = path;
            auto cmd = std::make_unique<AddPluginCommand>(
                m_project.tracks()[idx].pluginChain(), pluginJson, &m_pluginManager,
                static_cast<double>(m_engine.sampleRate()), m_engine.bufferSize());
            cmd->setBeforeRemoveCallback([this](PluginInstance* plugin) {
                closePluginWindowsFor(plugin);
            });
            executeCommand(std::move(cmd));
            if (auto* added = dynamic_cast<AddPluginCommand*>(m_undoStack.topCommand()))
                if (added->addedPlugin()) openPluginEditor(added->addedPlugin());
        });
        connect(row.pluginList, &PluginListWidget::pluginRemoved, this, [this](int) {
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });
        connect(row.pluginList, &PluginListWidget::pluginWillBeMoved, this, [this](int, int) {
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });
        connect(row.pluginList, &PluginListWidget::pluginWillBeToggled, this, [this] {
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });

        connect(row.view, &TrackViewWidget::eventDragStarted, this, [this] {
            // Pushed only for Ctrl/Shift-drag duplicates; plain moves and
            // cross-track moves are recorded by MoveEventCommand /
            // MoveEventToTrackCommand at drag release.
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });

        connect(row.view, &TrackViewWidget::cutEventRequested, this,
                [this, idx = trackIndex](int64_t eventId, int64_t cutSample, bool snapToGrid) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) return;
            double snapUnit = m_project.samplesPerBar() / m_snapResolution;
            executeCommand(std::make_unique<CutEventCommand>(
                m_project, idx, eventId, cutSample, snapToGrid, snapUnit));
        });

        connect(row.view, &TrackViewWidget::crossfadeRequested, this,
                [this, idx = trackIndex](const std::vector<int64_t>& eventIds,
                                         int64_t fadeSamples) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) return;
            if (m_project.tracks()[idx].type() != Track::Type::Audio) return;
            int64_t length = fadeSamples;
            if (length < 0)
                length = static_cast<int64_t>(std::llround(
                    m_project.sampleRate() * vvvdaw::DefaultCrossfadeMs / 1000.0));
            executeCommand(std::make_unique<SetEventsFadeCommand>(
                m_project, idx, eventIds, length));
        });

        connect(row.view, &TrackViewWidget::eventTrimFinished, this,
                [this, idx = trackIndex](int64_t eventId,
                                         int64_t oldStart, int64_t newStart,
                                         int64_t oldOffset, int64_t newOffset,
                                         int64_t oldDuration, int64_t newDuration) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size())) return;
            if (m_project.tracks()[idx].type() == Track::Type::Midi)
                pushCommand(std::make_unique<TrimMidiEventCommand>(
                    m_project, idx, eventId, oldStart, newStart,
                    oldOffset, oldDuration, newOffset, newDuration));
            else
                pushCommand(std::make_unique<TrimEventCommand>(
                    m_project, idx, eventId, oldStart, newStart,
                    oldOffset, oldDuration, newOffset, newDuration));
        });

        connect(row.view, &TrackViewWidget::takeSwitchStarted, this, [this] {
            pushCommand(std::make_unique<SnapshotCommand>(m_project));
        });

        connect(row.view, &TrackViewWidget::eventDoubleClicked, this,
                [this, idx = trackIndex](int64_t eventId) {
            openPianoRoll(idx, eventId);
        });

        connect(row.view, &TrackViewWidget::addMidiEventRequested, this,
                [this, idx = trackIndex](int64_t startSample) {
            if (idx < 0 || idx >= static_cast<int>(m_project.tracks().size()))
                return;
            QJsonObject clipJson;
            clipJson["ppq"] = MidiClip::kPPQ;
            clipJson["lengthTicks"] = static_cast<qint64>(MidiClip::kPPQ) * 4;
            QJsonObject eventJson;
            eventJson["startSample"] = static_cast<qint64>(startSample);
            eventJson["offsetSample"] = 0;
            eventJson["durationSample"] = static_cast<qint64>(m_project.samplesPerBar());
            eventJson["clip"] = clipJson;

            auto cmd = std::make_unique<AddMidiEventCommand>(m_project, idx, eventJson);
            executeCommand(std::move(cmd));
            if (auto* added = dynamic_cast<AddMidiEventCommand*>(m_undoStack.topCommand()))
                if (added->createdEventId() >= 0)
                    openPianoRoll(idx, added->createdEventId());
        });

        connect(row.view, &TrackViewWidget::dragInProgress, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t eventId, int64_t currentStartSample, QPoint globalPos) {
            QWidget* widget = QApplication::widgetAt(globalPos);
            Track& src = m_project.tracks()[srcIdx];
            const bool srcIsMidi = (src.type() == Track::Type::Midi);
            AudioEvent* audioEv = srcIsMidi ? nullptr : src.findEvent(eventId);
            MidiEvent* midiEv = srcIsMidi ? src.findMidiEvent(eventId) : nullptr;
            const bool hasDrag = (audioEv != nullptr || midiEv != nullptr);

            bool onDifferentTrack = false;
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                bool isTarget = (m_trackRows[t].view == widget && static_cast<int>(t) != srcIdx);
                bool compatible = isTarget && hasDrag
                                  && (m_project.tracks()[t].type() == src.type());
                if (compatible) {
                    if (audioEv)
                        m_trackRows[t].view->setDragPreview(audioEv, currentStartSample);
                    else
                        m_trackRows[t].view->setMidiDragPreview(midiEv, currentStartSample);
                    onDifferentTrack = true;
                } else {
                    m_trackRows[t].view->clearDragPreview();
                }
            }
            m_trackRows[srcIdx].view->setDragSourceVisible(!onDifferentTrack);
        });

        connect(row.view, &TrackViewWidget::eventDragFinished, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t eventId, int64_t oldStart, int64_t newStart,
                 QPoint globalPos, bool wasDuplicate) {
            for (auto& r : m_trackRows) {
                r.view->clearDragPreview();
                r.view->setDragSourceVisible(true);
            }

            QWidget* widget = QApplication::widgetAt(globalPos);
            int dstTrack = -1;
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                if (m_trackRows[t].view == widget && static_cast<int>(t) != srcIdx) {
                    dstTrack = static_cast<int>(t);
                    break;
                }
            }

            if (dstTrack >= 0) {
                moveEventToTrack(srcIdx, dstTrack, eventId, newStart);
                // Duplicates are already covered by the snapshot pushed at
                // drag start; only record the plain relocation.
                if (!wasDuplicate)
                    pushCommand(std::make_unique<MoveEventToTrackCommand>(
                        m_project, srcIdx, dstTrack, eventId, oldStart, newStart));
            } else if (!wasDuplicate) {
                if (m_project.tracks()[srcIdx].type() == Track::Type::Midi)
                    pushCommand(std::make_unique<MoveMidiEventCommand>(
                        m_project, srcIdx, eventId, oldStart, newStart));
                else
                    pushCommand(std::make_unique<MoveEventCommand>(
                        m_project, srcIdx, eventId, oldStart, newStart));
            }
        });

        connect(row.view, &TrackViewWidget::scrollOffsetChanged, this, [this](int64_t offset) {
            m_scrollOffset = offset;
            m_timelineRuler->setScrollOffset(offset);
            m_measureRuler->setScrollOffset(offset);
            for (auto& r : m_trackRows) {
                if (r.view)
                    r.view->setScrollOffset(offset);
            }
            m_horizontalScroll->blockSignals(true);
            m_horizontalScroll->setValue(static_cast<int>(offset / vvvdaw::ScrollStepSamples));
            m_horizontalScroll->blockSignals(false);
        });

        row.innerSplitter = new QSplitter(Qt::Horizontal, m_trackContainer);
        row.innerSplitter->setContentsMargins(0, 0, 0, 0);
        row.innerSplitter->addWidget(row.pluginList);
        row.innerSplitter->addWidget(row.view);
        row.innerSplitter->setStretchFactor(0, 0);
        row.innerSplitter->setStretchFactor(1, 1);
        row.innerSplitter->setSizes({m_savedPluginListWidth, 1000 - m_savedPluginListWidth});

        int splitterIndex = static_cast<int>(m_trackSplitters.size());
        m_trackSplitters.push_back(row.innerSplitter);

        auto* hbox = new QHBoxLayout;
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(0);
        hbox->addWidget(row.panel);
        hbox->addWidget(row.innerSplitter, 1);
        m_trackLayout->addLayout(hbox);

        connect(row.innerSplitter, &QSplitter::splitterMoved, this,
                [this, splitterIndex](int pos, int index) {
            Q_UNUSED(index);
            Q_UNUSED(pos);
            syncPluginListSplitters(splitterIndex);
        });

        m_trackRows.push_back(row);
}

void MainWindow::syncAfterRebuild() {
    m_trackLayout->addStretch();

    syncZoom();

    int64_t ph = m_engine.playPosition();
    syncPlayheadViews(ph);
    syncRecordingPreviews();

    bool snap = m_project.snapToGrid();
    m_timelineRuler->setSnapToGrid(snap);
    m_measureRuler->setSnapToGrid(snap);
    if (m_project.hasLoop()) {
        auto ls = m_project.loopStart();
        auto le = m_project.loopEnd();
        m_timelineRuler->setLoop(ls, le);
        m_measureRuler->setLoop(ls, le);
    } else {
        m_timelineRuler->clearLoop();
        m_measureRuler->clearLoop();
    }
    if (m_project.hasRecordRegion()) {
        auto rs = m_project.recordRegionStart();
        auto re = m_project.recordRegionEnd();
        m_timelineRuler->setRecordRegion(rs, re);
        m_measureRuler->setRecordRegion(rs, re);
    } else {
        m_timelineRuler->clearRecordRegion();
        m_measureRuler->clearRecordRegion();
    }
    m_transportPanel->setSnapToGrid(snap);

    m_measureRuler->setTempo(m_project.tempo());
    m_measureRuler->setTimeSignature(m_project.timeSigNum(), m_project.timeSigDen());
    m_measureRuler->setSampleRate(m_engine.sampleRate());
    m_timelineRuler->setSampleRate(m_engine.sampleRate());
    m_measureRuler->setScrollOffset(m_scrollOffset);

    m_tempoWidget->setTempo(m_project.tempo());
    m_tempoWidget->setTimeSignature(m_project.timeSigNum(), m_project.timeSigDen());
    m_tempoWidget->setMetronomeEnabled(m_project.metronomeEnabled());
    m_tempoWidget->setPrecountEnabled(m_project.precountEnabled());
    m_engine.setMetronomeEnabled(m_project.metronomeEnabled());
    m_engine.setPrecountEnabled(m_project.precountEnabled());

    syncSnapUnit();

    if (m_busPanel->isVisible())
        m_busPanel->rebuild();

    m_trackContainer->update();
}

void MainWindow::syncSnapUnit() {
    double snapUnit = m_project.samplesPerBar() / m_snapResolution;
    m_timelineRuler->setSnapUnit(snapUnit);
    m_measureRuler->setSnapUnit(snapUnit);
    m_measureRuler->setTempo(m_project.tempo());
    m_measureRuler->setTimeSignature(m_project.timeSigNum(), m_project.timeSigDen());
    for (auto& row : m_trackRows) {
        if (row.view) {
            row.view->setSnapUnit(snapUnit);
            row.view->setSamplesPerTick(m_project.samplesPerTick());
        }
    }
}

void MainWindow::syncZoom() {
    m_timelineRuler->setZoom(m_zoom);
    m_measureRuler->setZoom(m_zoom);
    for (auto& row : m_trackRows)
        row.view->setZoom(m_zoom);
}

void MainWindow::updateRulerSpacers(int panelWidth) {
    if (panelWidth < 100) panelWidth = 100;
    m_rulerSpacer1->setFixedWidth(panelWidth);
    m_rulerSpacer2->setFixedWidth(panelWidth);
    m_scrollSpacer->setFixedWidth(panelWidth);
}

void MainWindow::syncPluginListSplitters(int senderIndex) {
    if (m_syncingSplitters) return;
    m_syncingSplitters = true;

    if (senderIndex >= 0 && senderIndex < static_cast<int>(m_trackSplitters.size())) {
        auto* sender = m_trackSplitters[senderIndex];
        QList<int> sizes = sender->sizes();
        int pluginWidth = sizes.value(0, 200);

        for (auto* spl : m_trackSplitters) {
            if (spl != sender) {
                spl->setSizes({pluginWidth, 1000});
            }
        }
        updateRulerSpacers(200 + pluginWidth);
    }

    m_syncingSplitters = false;
}

void MainWindow::syncScrollPositions(int value) {
    m_scrollOffset = static_cast<int64_t>(value) * vvvdaw::ScrollStepSamples;
    m_timelineRuler->setScrollOffset(m_scrollOffset);
    m_measureRuler->setScrollOffset(m_scrollOffset);
    for (auto& row : m_trackRows)
        row.view->setScrollOffset(m_scrollOffset);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        for (auto* w : m_pianoRollWindows) {
            if (w == obj) {
                setActiveMidiPreview(w->trackIndex(), w->eventId());
                break;
            }
        }
    }
    if (obj == m_busPanelGrip) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            m_gripDragging = true;
            m_gripStartY = me->globalPosition().toPoint().y();
            m_gripStartHeight = m_busPanel->height();
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_gripDragging) {
            int delta = m_gripStartY - me->globalPosition().toPoint().y();
            int newH = qBound(80, m_gripStartHeight + delta, 600);
            m_busPanel->setFixedHeight(newH);
            m_settings.busPanelHeight = newH;
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_gripDragging = false;
            return true;
        }
    }
    if (obj == m_instrumentPanelGrip) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            m_gripDragging = true;
            m_gripStartY = me->globalPosition().toPoint().y();
            m_gripStartHeight = m_instrumentPanel->height();
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_gripDragging) {
            int delta = m_gripStartY - me->globalPosition().toPoint().y();
            int newH = qBound(100, m_gripStartHeight + delta, 600);
            m_instrumentPanel->setFixedHeight(newH);
            m_settings.instrumentPanelHeight = newH;
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_gripDragging = false;
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

PluginChain* MainWindow::findChainForPlugin(PluginInstance* plugin) {
    // Search all tracks
    for (auto& track : m_project.tracks()) {
        auto& chain = track.pluginChain();
        for (int j = 0; j < chain.count(); ++j)
            if (chain.plugin(j) == plugin) return &chain;
    }
    // Search all buses
    for (auto& bus : m_project.buses()) {
        for (int j = 0; j < bus.pluginChain().count(); ++j)
            if (bus.pluginChain().plugin(j) == plugin) return &bus.pluginChain();
    }
    // Search all instruments (synth + effects)
    for (auto& inst : m_project.instruments()) {
        if (inst.synth() == plugin)
            return const_cast<PluginChain*>(&inst.effects());
        for (int j = 0; j < inst.effects().count(); ++j)
            if (inst.effects().plugin(j) == plugin) return &inst.effects();
    }
    return nullptr;
}

void MainWindow::openPianoRoll(int trackIndex, int64_t eventId) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks().size()))
        return;
    MidiEvent* event = m_project.tracks()[trackIndex].findMidiEvent(eventId);
    if (!event || !event->activeClip()) return;

    for (auto* w : m_pianoRollWindows) {
        if (w->trackIndex() == trackIndex && w->eventId() == eventId) {
            w->raise();
            w->activateWindow();
            w->reload();
            return;
        }
    }

    auto* window = new PianoRollWindow(m_project, m_undoStack, m_engine, trackIndex, eventId, this);
    m_pianoRollWindows.push_back(window);
    window->installEventFilter(this);
    connect(window, &PianoRollWindow::windowClosed, this, [this, window]() {
        m_pianoRollWindows.erase(
            std::remove(m_pianoRollWindows.begin(), m_pianoRollWindows.end(), window),
            m_pianoRollWindows.end());
        updateMidiPreviewTarget();
    });
    connect(window, &PianoRollWindow::undoRequested, this, &MainWindow::performUndo);
    connect(window, &PianoRollWindow::redoRequested, this, &MainWindow::performRedo);
    connect(window, &PianoRollWindow::toggleSnapRequested, this, &MainWindow::toggleSnapToGrid);
    window->show();

    setActiveMidiPreview(trackIndex, eventId);
}

void MainWindow::setActiveMidiPreview(int trackIndex, int64_t eventId) {
    // Notes still held from the previous preview target (the keyboard is
    // physically held) must be released, otherwise they keep ringing: their
    // note-offs would be delivered to the new track and never match. The
    // release is flushed on the next audio block by injectPreviewMidi().
    m_engine.cancelPreviewNotes();
    m_midiPreviewTrack = trackIndex;
    m_engine.setMidiPreviewTrack(trackIndex);
    m_midiTargetHints[trackIndex] = eventId;
    m_engine.midiRecorder().setTargetHints(m_midiTargetHints);
}

void MainWindow::updateMidiPreviewTarget() {
    m_midiTargetHints.clear();
    for (auto* w : m_pianoRollWindows)
        m_midiTargetHints[w->trackIndex()] = w->eventId();
    m_engine.midiRecorder().setTargetHints(m_midiTargetHints);

    int target = m_midiPreviewTrack;
    if (m_midiTargetHints.count(target) == 0)
        target = m_pianoRollWindows.empty() ? -1 : m_pianoRollWindows.back()->trackIndex();
    if (target != m_midiPreviewTrack) {
        // The active piano-roll track changed (e.g. its window closed while
        // notes were held): release them so they cannot keep ringing.
        m_engine.cancelPreviewNotes();
        m_midiPreviewTrack = target;
        m_engine.setMidiPreviewTrack(target);
    }
}

void MainWindow::resyncPianoRollWindows() {
    std::vector<PianoRollWindow*> toClose;
    for (auto* w : m_pianoRollWindows) {
        if (!w->reload())
            toClose.push_back(w);
    }
    for (auto* w : toClose)
        w->close();
}

void MainWindow::openPluginEditor(PluginInstance* plugin) {
    if (!plugin || !plugin->hasEditor()) return;

    auto* chain = findChainForPlugin(plugin);

    auto* lv2 = dynamic_cast<LV2Instance*>(plugin);
    if (lv2 && lv2->hasNativeUI()) {
        // Use plugin window path for embedding native X11 UIs.
        // The LV2Instance::createEditor will reparent into PluginWindow.
    }

    for (auto* w : m_pluginWindows) {
        if (w->plugin() == plugin && w->isVisible()) {
            w->raise();
            w->activateWindow();
            return;
        }
    }
    auto* window = new PluginWindow(plugin, m_settings.pluginKnobsPerRow, this);
    m_pluginWindows.push_back(window);
    connect(window, &PluginWindow::windowClosed, this, [this, window, plugin]() {
        plugin->setParameterChangeCallback({});
        plugin->setStringParameterChangeCallback({});
        m_pluginWindows.erase(
            std::remove(m_pluginWindows.begin(), m_pluginWindows.end(), window),
            m_pluginWindows.end());
    });
    connect(window, &PluginWindow::parameterChangeRequested, this,
            [this, chain, plugin](int paramIndex, float oldValue, float newValue) {
        if (chain)
            m_undoStack.execute(
                std::make_unique<SetPluginParameterCommand>(*chain, plugin, paramIndex, oldValue, newValue));
    });
    connect(window, &PluginWindow::pathParameterChangeRequested, this,
            [this, chain, plugin](int paramIndex, const QString& oldValue, const QString& newValue) {
        if (chain)
            m_undoStack.execute(
                std::make_unique<SetPluginPathParameterCommand>(*chain, plugin, paramIndex, oldValue, newValue));
    });
    plugin->setParameterChangeCallback(
        [this, chain, plugin](int paramIndex, float oldValue, float newValue) {
        if (chain)
            m_undoStack.execute(
                std::make_unique<SetPluginParameterCommand>(*chain, plugin, paramIndex, oldValue, newValue));
    });
    plugin->setStringParameterChangeCallback(
        [this, chain, plugin](int paramIndex, const QString& oldValue, const QString& newValue) {
        if (chain && oldValue != newValue)
            m_undoStack.execute(
                std::make_unique<SetPluginPathParameterCommand>(*chain, plugin, paramIndex, oldValue, newValue));
    });
    window->open();
}
