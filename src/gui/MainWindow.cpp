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


void MainWindow::executeCommand(std::unique_ptr<UndoCommand> cmd) {
    {
        // The audio thread reads the project under a shared lock; hold the
        // write lock so a command that destroys tracks/buses/plugin chains
        // cannot race a running callback.
        auto lock = m_project.writeLock();
        m_undoStack.execute(std::move(cmd));
    }
    rebuildTracks();
    refreshAfterProjectMutation();
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
        refreshAfterProjectMutation();
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
        refreshAfterProjectMutation();
        if (m_busPanel->isVisible())
            m_busPanel->rebuild();
    }
}


void MainWindow::refreshAfterProjectMutation() {
    refreshBusCombos();
    resyncPianoRollWindows();
    m_engine.refreshMidiOutputs();
    if (m_instrumentPanel->isVisible())
        m_instrumentPanel->rebuild();
}


void MainWindow::toggleSnapToGrid() {
    // Route through the transport panel so the button state, the project
    // flag, track views and rulers all stay in sync.
    m_transportPanel->setSnapToGrid(!m_project.snapToGrid());
}


void MainWindow::loadStyleSheet() {
    QFile qss(":/resources/style.qss");
    if (qss.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
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


bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        for (auto* w : m_pianoRollWindows) {
            if (w == obj) {
                setActiveMidiPreview(w->trackIndex(), w->eventId());
                break;
            }
        }
    }
    if (obj == m_busPanelGrip)
        return handleGripDrag(m_busPanel, m_settings.busPanelHeight, 80, event);
    if (obj == m_instrumentPanelGrip)
        return handleGripDrag(m_instrumentPanel, m_settings.instrumentPanelHeight, 100, event);
    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::handleGripDrag(QWidget* panel, int& heightSetting,
                                int minHeight, const QEvent* event) {
    auto* me = static_cast<const QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
        m_gripDragging = true;
        m_gripStartY = me->globalPosition().toPoint().y();
        m_gripStartHeight = panel->height();
        return true;
    }
    if (event->type() == QEvent::MouseMove && m_gripDragging) {
        int delta = m_gripStartY - me->globalPosition().toPoint().y();
        int newH = qBound(minHeight, m_gripStartHeight + delta, 600);
        panel->setFixedHeight(newH);
        heightSetting = newH;
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        m_gripDragging = false;
        return true;
    }
    return false;
}
