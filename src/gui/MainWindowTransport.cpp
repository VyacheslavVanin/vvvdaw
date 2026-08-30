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
        int pluginWidth = sizes.value(0, vvvdaw::DefaultPluginPanelWidth);

        for (auto* spl : m_trackSplitters) {
            if (spl != sender) {
                spl->setSizes({pluginWidth, 1000});
            }
        }
        // Persist the (shared) panel width so it survives a project reload.
        const int n = static_cast<int>(m_project.tracks().size());
        for (int i = 0; i < static_cast<int>(m_trackSplitters.size()) && i < n; ++i) {
            m_project.tracks()[i].setPluginPanelWidth(pluginWidth);
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
