#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransportPanel.h"
#include "TimelineRuler.h"
#include "MeasureRuler.h"
#include "TempoWidget.h"
#include "TrackPanelWidget.h"
#include "TrackViewWidget.h"
#include "BusPanelWidget.h"
#include "PluginListWidget.h"
#include "PluginWindow.h"
#include "core/UndoStack.h"
#include "core/TimeUtils.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "audio/AudioEngine.h"
#include "audio/DeviceInfo.h"
#include "core/Settings.h"
#include "plugin/PluginInstance.h"

using vvvdaw::TransportState;
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(Project& project, AudioEngine& engine, Settings& settings,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_engine(engine)
    , m_settings(settings)
{
    setWindowTitle("vvvdaw — " + m_project.name());
    resize(1400, 800);

    m_pluginManager.loadCache();
    if (m_pluginManager.plugins().empty()) {
        m_pluginManager.scanDirectories(
            Settings().pluginScanPaths.empty()
                ? PluginManager::defaultScanPaths()
                : Settings().pluginScanPaths);
        m_pluginManager.scanLV2();
    } else {
        m_pluginManager.ensureLV2Loaded();
    }
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
    auto* rulerSpacer1 = new QWidget(this);
    rulerSpacer1->setFixedWidth(200);
    rulerSpacer1->setStyleSheet("background-color: #2a2a2a;");
    m_timelineRuler = new TimelineRuler(this);
    rulerRow1->addWidget(rulerSpacer1);
    rulerRow1->addWidget(m_timelineRuler, 1);
    layout->addLayout(rulerRow1);

    // Ruler row 2
    auto* rulerRow2 = new QHBoxLayout;
    rulerRow2->setContentsMargins(0, 0, 0, 0);
    rulerRow2->setSpacing(0);
    auto* rulerSpacer2 = new QWidget(this);
    rulerSpacer2->setFixedWidth(200);
    rulerSpacer2->setStyleSheet("background-color: #252525;");
    m_measureRuler = new MeasureRuler(this);
    rulerRow2->addWidget(rulerSpacer2);
    rulerRow2->addWidget(m_measureRuler, 1);
    layout->addLayout(rulerRow2);

    auto onPlayheadClicked = [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
        m_timelineRuler->setPlayheadPosition(sample);
        m_measureRuler->setPlayheadPosition(sample);
        for (auto& row : m_trackRows)
            row.view->setPlayheadPosition(sample);
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
    auto updateSnapUnit = [this] {
        double snapUnit = m_project.samplesPerBar() / m_snapResolution;
        m_timelineRuler->setSnapUnit(snapUnit);
        m_measureRuler->setSnapUnit(snapUnit);
        m_measureRuler->setTempo(m_project.tempo());
        m_measureRuler->setTimeSignature(m_project.timeSigNum(), m_project.timeSigDen());
        for (auto& row : m_trackRows) {
            if (row.view) {
                row.view->setSnapUnit(snapUnit);
            }
        }
    };
    connect(m_tempoWidget, &TempoWidget::tempoChanged, this, [this, updateSnapUnit](double bpm) {
        pushUndoState();
        m_project.setTempo(bpm);
        updateSnapUnit();
    });
    connect(m_tempoWidget, &TempoWidget::timeSignatureChanged, this, [this, updateSnapUnit](int num, int den) {
        pushUndoState();
        m_project.setTimeSignature(num, den);
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

    // Bus panel
    m_busPanel = new BusPanelWidget(m_project, this);
    m_busPanel->hide();
    layout->addWidget(m_busPanel);

    connect(m_busPanel, &BusPanelWidget::addBusRequested, this, [this] {
        pushUndoState();
        AudioBus newBus;
        newBus.name = QString("Bus %1").arg(m_project.buses().size());
        newBus.volume = 1.0f;
        newBus.pan = 0.0f;
        newBus.outputBusIndex = 0;
        m_project.addBus(std::move(newBus));
        m_busPanel->rebuild();
        refreshBusCombos();
    });

    connect(m_busPanel, &BusPanelWidget::removeBusRequested, this, [this](int index) {
        if (index <= 0 || index >= static_cast<int>(m_project.buses().size()))
            return;
        if (!m_project.buses()[index].removable)
            return;
        pushUndoState();
        m_project.removeBus(index);
        m_busPanel->rebuild();
        refreshBusCombos();
    });

    connect(m_busPanel, &BusPanelWidget::busChanged, this, [this] {
        pushUndoState();
    });

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
    auto* scrollSpacer = new QWidget(this);
    scrollSpacer->setFixedWidth(200);
    scrollSpacer->setStyleSheet("background-color: #2a2a2a;");
    scrollRow->addWidget(scrollSpacer);
    scrollRow->addWidget(m_horizontalScroll);
    layout->addLayout(scrollRow);

    setCentralWidget(central);

    setupTransportConnections();
    setupTimer();
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
        int64_t zeroPos = 0;
        m_timelineRuler->setPlayheadPosition(zeroPos);
        m_measureRuler->setPlayheadPosition(zeroPos);
        for (auto& row : m_trackRows)
            row.view->setPlayheadPosition(zeroPos);
        m_transportPanel->setPlaying(false);
        m_transportPanel->setRecording(false);
        refreshTrackViews();
    });

    connect(m_transportPanel, &TransportPanel::recordClicked, this, [this, refreshTrackViews] {
        TransportState s = m_engine.transportState();
        if (s == TransportState::Recording) {
            m_engine.setTransportState(TransportState::Stopped);
            m_transportPanel->setRecording(false);
            refreshTrackViews();
        } else {
            m_engine.setTransportState(TransportState::Recording);
            m_transportPanel->setRecording(true);
        }
    });

    auto* recordShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    connect(recordShortcut, &QShortcut::activated, this, [this, refreshTrackViews] {
        TransportState s = m_engine.transportState();
        if (s == TransportState::Recording) {
            m_engine.setTransportState(TransportState::Stopped);
            m_transportPanel->setRecording(false);
            refreshTrackViews();
        } else {
            m_engine.setTransportState(TransportState::Recording);
            m_transportPanel->setRecording(true);
        }
    });

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

        m_timelineRuler->setPlayheadPosition(pos);
        m_measureRuler->setPlayheadPosition(pos);
        for (auto& row : m_trackRows)
            row.view->setPlayheadPosition(pos);
    });
    timer->start(40);
}

void MainWindow::pushUndoState() {
    m_undoStack.push(m_project.toJson());
}

void MainWindow::performUndo() {
    applyState(m_undoStack.undo(m_project.toJson()));
}

void MainWindow::performRedo() {
    applyState(m_undoStack.redo(m_project.toJson()));
}

void MainWindow::applyState(const std::optional<QJsonObject>& state) {
    if (!state) return;
    m_engine.setTransportState(TransportState::Stopped);
    m_engine.setProject(nullptr);
    m_project.fromJson(*state);
    m_engine.setProject(&m_project);
    rebuildTracks();
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Project", QKeySequence::New);
    auto* openAction = fileMenu->addAction("&Open Project...", QKeySequence::Open);
    auto* saveAction = fileMenu->addAction("&Save Project", QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"));
    fileMenu->addSeparator();
    auto* settingsAction = fileMenu->addAction("&Settings...");
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("&Quit", QKeySequence::Quit);

    connect(newAction, &QAction::triggered, this, [this] {
        m_engine.setTransportState(TransportState::Stopped);
        m_engine.setProject(nullptr);
        m_undoStack.clear();
        m_project = Project();
        m_project.addTrack("Track 1");
        m_engine.setProject(&m_project);
        m_project.setSampleRate(m_engine.sampleRate());
        m_scrollOffset = 0;
        rebuildTracks();
        setWindowTitle("vvvdaw - Untitled");
    });

    connect(openAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Open Project",
            QString(), "Project Files (project.json)");
        if (path.isEmpty()) return;

        m_engine.setTransportState(TransportState::Stopped);
        m_engine.setProject(nullptr);
        m_undoStack.clear();
        Project newProject;
        if (!newProject.load(path)) {
            QMessageBox::warning(this, "Error", "Failed to load project.");
            return;
        }
        m_project = std::move(newProject);
        m_engine.setProject(&m_project);
        m_project.setSampleRate(m_engine.sampleRate());
        m_scrollOffset = 0;
        rebuildTracks();
        setWindowTitle("vvvdaw - " + QFileInfo(path).absolutePath());
    });

    connect(saveAction, &QAction::triggered, this, [this, saveAsAction] {
        if (m_project.filePath().isEmpty()) {
            saveAsAction->trigger();
            return;
        }
        if (!m_project.save(m_project.filePath())) {
            QMessageBox::warning(this, "Error", "Failed to save project.");
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
        setWindowTitle("vvvdaw - " + dir);
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
    toggleBusPanelAction->setChecked(false);
    connect(toggleBusPanelAction, &QAction::triggered, this, [this](bool checked) {
        m_busPanel->setVisible(checked);
        if (checked)
            m_busPanel->rebuild();
    });

    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(m_settings, m_engine, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_engine.shutdown();
            m_engine.init(m_settings);
            m_engine.startStream();
            m_project.setSampleRate(m_engine.sampleRate());
            for (auto& row : m_trackRows)
                row.view->setMouseWheelScroll(m_settings.mouseWheelScroll);
        }
    });

    auto* trackMenu = menuBar()->addMenu("&Track");
    auto* addTrackAction = trackMenu->addAction("&Add Track", QKeySequence("Ctrl+T"));
    connect(addTrackAction, &QAction::triggered, this, [this] {
        pushUndoState();
        m_project.addTrack();
        rebuildTracks();
    });

    auto* deleteTrackAction = trackMenu->addAction("&Delete Track");
    connect(deleteTrackAction, &QAction::triggered, this, [this] {
        pushUndoState();
        for (size_t i = 0; i < m_project.tracks().size(); ++i) {
            if (m_trackRows[i].panel->hasFocus() || m_trackRows[i].view->hasFocus()) {
                QMetaObject::invokeMethod(this, [this, i] {
                    if (i < m_project.tracks().size()) {
                        m_project.removeTrack(static_cast<int>(i));
                        rebuildTracks();
                    }
                }, Qt::QueuedConnection);
                return;
            }
        }
        for (size_t i = m_project.tracks().size(); i > 0; --i) {
            if (m_trackRows[i - 1].view->selectedEventIndex() >= 0) {
                int idx = static_cast<int>(i - 1);
                QMetaObject::invokeMethod(this, [this, idx] {
                    if (idx < static_cast<int>(m_project.tracks().size())) {
                        m_project.removeTrack(idx);
                        rebuildTracks();
                    }
                }, Qt::QueuedConnection);
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
            if (row.view->selectedEventIndex() >= 0) {
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
    for (auto& row : m_trackRows) {
        if (row.panel) {
            row.panel->updateBusList(m_project.buses());
            row.panel->updateInputDeviceList(devices);
        }
    }
}

void MainWindow::rebuildTracks() {
    for (auto& row : m_trackRows) {
        if (row.panel) {
            row.panel->hide();
            row.panel->deleteLater();
        }
        if (row.view) {
            row.view->hide();
            row.view->deleteLater();
        }
    }
    m_trackRows.clear();

    while (auto* item = m_trackLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    auto devices = AudioEngine::enumerateInputDevices();

    for (auto& track : m_project.tracks()) {
        TrackRow row;
        bool odd = (static_cast<int>(&track - m_project.tracks().data()) % 2) != 0;
        row.panel = new TrackPanelWidget(&track, m_trackContainer);
        row.panel->setAlternateRow(odd);
        row.panel->updateBusList(m_project.buses());
        row.panel->updateInputDeviceList(devices);
        row.panel->setPluginManager(&m_pluginManager);
        row.panel->pluginList()->setAudioParams(m_engine.sampleRate(), m_engine.bufferSize());
        row.panel->updateFromTrack();
        row.view = new TrackViewWidget(&track, m_trackContainer);
        row.view->setAlternateRow(odd);
        row.view->setZoom(m_zoom);
        row.view->setScrollOffset(m_scrollOffset);
        row.view->setSnapToGrid(m_project.snapToGrid());
        row.view->setMouseWheelScroll(m_settings.mouseWheelScroll);

        connect(row.view, &TrackViewWidget::zoomChanged, this, [this](double zoom) {
            m_zoom = zoom;
            syncZoom();
        });

        connect(row.panel, &TrackPanelWidget::addTrackRequested, this, [this] {
            pushUndoState();
            m_project.addTrack();
            rebuildTracks();
        });

        connect(row.panel, &TrackPanelWidget::deleteRequested, this, [this, idx = static_cast<int>(&track - m_project.tracks().data())] {
            if (idx < static_cast<int>(m_project.tracks().size())) {
                pushUndoState();
                m_project.removeTrack(idx);
                rebuildTracks();
            }
        });

        connect(row.panel, &TrackPanelWidget::beforeModify, this, [this] {
            pushUndoState();
        });

        connect(row.panel, &TrackPanelWidget::openPluginEditorRequested, this,
                &MainWindow::openPluginEditor);

        connect(row.view, &TrackViewWidget::eventDragStarted, this, [this] {
            pushUndoState();
        });

        connect(row.view, &TrackViewWidget::eventEdgeTrimStarted, this, [this] {
            pushUndoState();
        });

        connect(row.view, &TrackViewWidget::takeSwitchStarted, this, [this] {
            pushUndoState();
        });

        connect(row.view, &TrackViewWidget::dragInProgress, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t eventId, int64_t currentStartSample, QPoint globalPos) {
            QWidget* widget = QApplication::widgetAt(globalPos);
            AudioEvent* ev = m_project.tracks()[srcIdx].findEvent(eventId);
            bool onDifferentTrack = false;
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                bool isTarget = (m_trackRows[t].view == widget && static_cast<int>(t) != srcIdx);
                if (isTarget && ev) {
                    m_trackRows[t].view->setDragPreview(ev, currentStartSample);
                    onDifferentTrack = true;
                } else {
                    m_trackRows[t].view->setDragPreview(nullptr, 0);
                }
            }
            m_trackRows[srcIdx].view->setDragSourceVisible(!onDifferentTrack);
        });

        connect(row.view, &TrackViewWidget::eventDragFinished, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t eventId, int64_t newStartSample, QPoint globalPos) {
            for (auto& r : m_trackRows) {
                r.view->setDragPreview(nullptr, 0);
                r.view->setDragSourceVisible(true);
            }

            QWidget* widget = QApplication::widgetAt(globalPos);
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                if (m_trackRows[t].view == widget && static_cast<int>(t) != srcIdx) {
                    Track& src = m_project.tracks()[srcIdx];
                    Track& dst = m_project.tracks()[t];
                    AudioEvent* ev = src.findEvent(eventId);
                    if (ev) {
                        ev->setStartSample(newStartSample);
                        dst.importEvent(*ev);
                        src.removeEvent(eventId);
                        m_trackRows[srcIdx].view->updateFromTrack();
                        m_trackRows[t].view->updateFromTrack();
                    }
                    break;
                }
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

        auto* hbox = new QHBoxLayout;
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(0);
        hbox->addWidget(row.panel);
        hbox->addWidget(row.view, 1);
        m_trackLayout->addLayout(hbox);

        m_trackRows.push_back(row);
    }

    m_trackLayout->addStretch();

    syncZoom();

    int64_t ph = m_engine.playPosition();
    m_timelineRuler->setPlayheadPosition(ph);
    m_measureRuler->setPlayheadPosition(ph);
    for (auto& row : m_trackRows)
        row.view->setPlayheadPosition(ph);

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

    double snapUnit = m_project.samplesPerBar() / m_snapResolution;
    m_timelineRuler->setSnapUnit(snapUnit);
    m_measureRuler->setSnapUnit(snapUnit);
    for (auto& row : m_trackRows) {
        if (row.view)
            row.view->setSnapUnit(snapUnit);
    }

    if (m_busPanel->isVisible())
        m_busPanel->rebuild();

    m_trackContainer->update();
}

void MainWindow::syncZoom() {
    m_timelineRuler->setZoom(m_zoom);
    m_measureRuler->setZoom(m_zoom);
    for (auto& row : m_trackRows)
        row.view->setZoom(m_zoom);
}

void MainWindow::syncScrollPositions(int value) {
    m_scrollOffset = static_cast<int64_t>(value) * vvvdaw::ScrollStepSamples;
    m_timelineRuler->setScrollOffset(m_scrollOffset);
    m_measureRuler->setScrollOffset(m_scrollOffset);
    for (auto& row : m_trackRows)
        row.view->setScrollOffset(m_scrollOffset);
}

void MainWindow::openPluginEditor(PluginInstance* plugin) {
    if (!plugin || !plugin->hasEditor()) return;
    for (auto* w : m_pluginWindows) {
        if (w->plugin() == plugin && w->isVisible()) {
            w->raise();
            w->activateWindow();
            return;
        }
    }
    auto* window = new PluginWindow(plugin, this);
    m_pluginWindows.push_back(window);
    connect(window, &PluginWindow::windowClosed, this, [this, window]() {
        m_pluginWindows.erase(
            std::remove(m_pluginWindows.begin(), m_pluginWindows.end(), window),
            m_pluginWindows.end());
    });
    window->open();
}
