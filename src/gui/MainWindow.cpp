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
        m_timelineRuler->setPlayheadPosition(sample);
        for (auto& row : m_trackRows)
            row.view->setPlayheadPosition(sample);
        auto updateTime = [&](int64_t pos) {
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
        };
        updateTime(sample);
    });

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
        m_engine.setPlayPosition(0);
        int64_t zeroPos = 0;
        m_timelineRuler->setPlayheadPosition(zeroPos);
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

    // Spacebar toggles play/pause
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

        if (s == TransportState::Playing || s == TransportState::Recording) {
            // Auto-scroll only during playback or recording
            int64_t viewWidth = m_trackContainer->width();
            double pixelPos = pos * m_zoom;
            double viewEnd = m_scrollOffset * m_zoom + viewWidth * 0.7;
            if (pixelPos > viewEnd) {
                m_scrollOffset = pos - static_cast<int64_t>(viewWidth * 0.3 / m_zoom);
                if (m_scrollOffset < 0) m_scrollOffset = 0;
                syncScrollPositions(static_cast<int>(m_scrollOffset / 48));
            }
        }

        // Update playhead always
        m_timelineRuler->setPlayheadPosition(pos);
        for (auto& row : m_trackRows)
            row.view->setPlayheadPosition(pos);
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

    auto* deleteTrackAction = trackMenu->addAction("&Delete Track");
    connect(deleteTrackAction, &QAction::triggered, this, [this] {
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

void MainWindow::rebuildTracks() {
    m_trackRows.clear();

    // Clear layout items recursively — delete widgets and wrappers, keep sub-layouts
    // (they'll be cleaned up when m_trackContainer is destroyed)
    struct Cleaner {
        static void run(QLayout* layout) {
            if (!layout) return;
            while (auto* item = layout->takeAt(0)) {
                if (auto* w = item->widget())
                    delete w;
                else if (auto* sub = item->layout())
                    Cleaner::run(sub);
                delete item;
            }
        }
    };
    Cleaner::run(m_trackLayout);

    for (auto& track : m_project.tracks()) {
        TrackRow row;
        bool odd = (static_cast<int>(&track - m_project.tracks().data()) % 2) != 0;
        row.panel = new TrackPanelWidget(&track, m_trackContainer);
        row.panel->setAlternateRow(odd);
        row.panel->updateFromTrack();
        row.view = new TrackViewWidget(&track, m_trackContainer);
        row.view->setAlternateRow(odd);
        row.view->setZoom(m_zoom);
        row.view->setScrollOffset(m_scrollOffset);

        connect(row.panel, &TrackPanelWidget::addTrackRequested, this, [this] {
            m_project.addTrack();
            rebuildTracks();
        });

        connect(row.panel, &TrackPanelWidget::deleteRequested, this, [this, idx = static_cast<int>(&track - m_project.tracks().data())] {
            if (idx < static_cast<int>(m_project.tracks().size())) {
                m_project.removeTrack(idx);
                rebuildTracks();
            }
        });

        connect(row.view, &TrackViewWidget::dragInProgress, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t /*eventId*/, int64_t /*currentStartSample*/, QPoint globalPos) {
            QWidget* widget = QApplication::widgetAt(globalPos);
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                bool hover = (m_trackRows[t].view == widget);
                m_trackRows[t].view->setDragHovered(hover && static_cast<int>(t) != srcIdx);
            }
        });

        connect(row.view, &TrackViewWidget::eventDragFinished, this,
                [this, srcIdx = static_cast<int>(&track - m_project.tracks().data())]
                (int64_t eventId, int64_t newStartSample, QPoint globalPos) {
            // Clear drag hover on all views
            for (auto& r : m_trackRows)
                r.view->setDragHovered(false);

            QWidget* widget = QApplication::widgetAt(globalPos);
            for (size_t t = 0; t < m_trackRows.size(); ++t) {
                if (m_trackRows[t].view == widget && static_cast<int>(t) != srcIdx) {
                    Track& src = m_project.tracks()[srcIdx];
                    Track& dst = m_project.tracks()[t];
                    AudioEvent* ev = src.findEvent(eventId);
                    if (ev) {
                        ev->startSample = newStartSample;
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

    // Restore playhead on new views
    int64_t ph = m_engine.playPosition();
    m_timelineRuler->setPlayheadPosition(ph);
    for (auto& row : m_trackRows)
        row.view->setPlayheadPosition(ph);
}

void MainWindow::syncScrollPositions(int value) {
    m_scrollOffset = static_cast<int64_t>(value) * 48;
    m_timelineRuler->setScrollOffset(m_scrollOffset);
    for (auto& row : m_trackRows)
        row.view->setScrollOffset(m_scrollOffset);
}
