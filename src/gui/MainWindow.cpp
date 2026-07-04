#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransportPanel.h"
#include "TimelineRuler.h"
#include "TrackPanelWidget.h"
#include "TrackViewWidget.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "audio/AudioEngine.h"
#include "core/Settings.h"
#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
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
    setupUi();
    setupMenus();
    loadStyleSheet();

    m_engine.setProject(&m_project);
    rebuildTracks();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_transportPanel = new TransportPanel(this);
    layout->addWidget(m_transportPanel);

    auto* rulerRow = new QHBoxLayout;
    rulerRow->setContentsMargins(0, 0, 0, 0);
    rulerRow->setSpacing(0);
    auto* rulerSpacer = new QWidget(this);
    rulerSpacer->setFixedWidth(200);
    rulerSpacer->setStyleSheet("background-color: #2a2a2a;");
    m_timelineRuler = new TimelineRuler(this);
    rulerRow->addWidget(rulerSpacer);
    rulerRow->addWidget(m_timelineRuler, 1);
    layout->addLayout(rulerRow);

    connect(m_timelineRuler, &TimelineRuler::playheadClicked, this, [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
    });

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_trackContainer = new QWidget(scrollArea);
    m_trackLayout = new QVBoxLayout(m_trackContainer);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(2);
    scrollArea->setWidget(m_trackContainer);

    layout->addWidget(scrollArea, 1);

    m_horizontalScroll = new QScrollBar(Qt::Horizontal, this);
    m_horizontalScroll->setRange(0, 1000000);
    connect(m_horizontalScroll, &QScrollBar::valueChanged, this, &MainWindow::syncScrollPositions);
    layout->addWidget(m_horizontalScroll);

    setCentralWidget(central);

    // Wire transport
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

    connect(m_transportPanel, &TransportPanel::forwardClicked, this, [this] {
        int64_t maxEnd = 0;
        for (const auto& track : m_project.tracks()) {
            for (const auto& event : track.events()) {
                int64_t end = event.startSample + event.durationSample;
                if (end > maxEnd) maxEnd = end;
            }
        }
        m_engine.setPlayPosition(maxEnd > 0 ? maxEnd : 48000 * 30);
    });

    // Timer for position updates
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        TransportState s = m_engine.transportState();
        if (s == TransportState::Playing || s == TransportState::Recording || s == TransportState::Paused) {
            int64_t pos = m_engine.playPosition();
            int sr = m_engine.sampleRate();
            int totalMs = static_cast<int>((pos * 1000) / sr);
            int hours = totalMs / 3600000;
            int mins = (totalMs % 3600000) / 60000;
            int secs = (totalMs % 60000) / 1000;
            int ms = totalMs % 1000;
            m_transportPanel->setTimeText(QString("%1:%2:%3.%4")
                .arg(hours, 2, 10, QChar('0'))
                .arg(mins, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'))
                .arg(ms, 3, 10, QChar('0')));

            // Auto-scroll
            int64_t viewWidth = m_trackContainer->width();
            double pixelPos = pos * m_zoom;
            double viewEnd = m_scrollOffset * m_zoom + viewWidth * 0.7;
            if (pixelPos > viewEnd) {
                m_scrollOffset = pos - static_cast<int64_t>(viewWidth * 0.3 / m_zoom);
                if (m_scrollOffset < 0) m_scrollOffset = 0;
                syncScrollPositions(static_cast<int>(m_scrollOffset / 48));
            }

            // Update playhead
            m_timelineRuler->setPlayheadPosition(pos);
            for (auto& row : m_trackRows)
                row.view->setPlayheadPosition(pos);
        }
    });
    timer->start(40);
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Project", QKeySequence::New);
    auto* saveAction = fileMenu->addAction("&Save Project", QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"));
    fileMenu->addSeparator();
    auto* settingsAction = fileMenu->addAction("&Settings...");
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("&Quit", QKeySequence::Quit);

    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(m_settings, m_engine, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_engine.shutdown();
            m_engine.init(m_settings);
            m_engine.startStream();
        }
    });

    auto* trackMenu = menuBar()->addMenu("&Track");
    auto* addTrackAction = trackMenu->addAction("&Add Track", QKeySequence("Ctrl+T"));
    connect(addTrackAction, &QAction::triggered, this, [this] {
        m_project.addTrack();
        rebuildTracks();
    });
}

void MainWindow::loadStyleSheet() {
    QFile qss(":/resources/style.qss");
    if (qss.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
}

void MainWindow::rebuildTracks() {
    for (auto& row : m_trackRows) {
        m_trackLayout->removeWidget(row.panel);
        m_trackLayout->removeWidget(row.view);
        delete row.panel;
        delete row.view;
    }
    m_trackRows.clear();

    // Remove existing layouts
    while (m_trackLayout->count() > 0) {
        auto* item = m_trackLayout->takeAt(0);
        if (item->layout()) {
            while (item->layout()->count() > 0) {
                auto* child = item->layout()->takeAt(0);
                delete child;
            }
        }
        delete item;
    }

    for (auto& track : m_project.tracks()) {
        TrackRow row;
        row.panel = new TrackPanelWidget(&track, m_trackContainer);
        row.panel->updateFromTrack();
        row.view = new TrackViewWidget(&track, m_trackContainer);
        row.view->setZoom(m_zoom);
        row.view->setScrollOffset(m_scrollOffset);

        connect(row.view, &TrackViewWidget::scrollOffsetChanged, this, [this](int64_t offset) {
            m_scrollOffset = offset;
            m_timelineRuler->setScrollOffset(offset);
            for (auto& r : m_trackRows) {
                if (r.view)
                    r.view->setScrollOffset(offset);
            }
            m_horizontalScroll->blockSignals(true);
            m_horizontalScroll->setValue(static_cast<int>(offset / 48));
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
}

void MainWindow::syncScrollPositions(int value) {
    m_scrollOffset = static_cast<int64_t>(value) * 48;
    m_timelineRuler->setScrollOffset(m_scrollOffset);
    for (auto& row : m_trackRows)
        row.view->setScrollOffset(m_scrollOffset);
}
