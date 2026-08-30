#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransportPanel.h"
#include "TimelineRuler.h"
#include "MeasureRuler.h"
#include "TempoWidget.h"
#include "TrackPanelWidget.h"
#include "TrackRowWidget.h"
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
#include "model/AudioBus.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "audio/AudioEngine.h"
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
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QShortcut>
#include <QTimer>
#include <QWidget>
#include <QFrame>
#include <QJsonArray>
#include <cmath>
#include <numeric>

namespace {
// Move the track at `src` so it sits right before the position `dst` in the
// ordering (`dst` is a 0..n index into the original list).
std::vector<int> moveTrackOrder(const std::vector<int>& order, int src, int dst) {
    std::vector<int> out;
    out.reserve(order.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (i != src) out.push_back(order[i]);
    }
    int insertPos = (src < dst) ? dst - 1 : dst;
    insertPos = qBound(0, insertPos, static_cast<int>(out.size()));
    out.insert(out.begin() + insertPos, order[src]);
    return out;
}
}

void MainWindow::rebuildTracks() {
    teardownTrackRows();

    std::vector<std::pair<int, QString>> midiOutList;
    std::vector<QString> instrumentNames;
    collectDeviceLists(midiOutList, instrumentNames);

    for (int i = 0; i < static_cast<int>(m_project.tracks().size()); ++i) {
        bool odd = (i % 2) != 0;
        buildTrackRow(i, odd, midiOutList, instrumentNames);
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
    m_trackRows.clear();
    m_trackSplitters.clear();

    while (auto* item = m_trackLayout->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    hideTrackInsertionLine();
}


void MainWindow::buildTrackRow(int trackIndex, bool odd,
                               const std::vector<std::pair<int, QString>>& midiOutList,
                               const std::vector<QString>& instrumentNames) {
    Track& track = m_project.tracks()[trackIndex];
    TrackRow row;
        row.row = new TrackRowWidget(m_trackContainer);
        row.row->setTrackIndex(trackIndex);

        row.panel = new TrackPanelWidget(&track, row.row);
        row.panel->setAlternateRow(odd);
        row.panel->updateBusList(m_project.buses());
        row.panel->updateMidiOutputs(midiOutList, instrumentNames);
        row.panel->updateFromTrack();

        row.pluginList = new PluginListWidget(row.row);
        row.pluginList->setTrack(&track);
        row.pluginList->setHeaderLabel("Effects:");
        row.pluginList->setPluginManager(&m_pluginManager);
        row.pluginList->setAudioParams(m_engine.sampleRate(), m_engine.bufferSize());
        row.pluginList->rebuild();

        row.view = new TrackViewWidget(&track, &m_project, row.row);
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
            addMidiEvent(idx, start);
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
            addPluginToChain(m_project.tracks()[idx].pluginChain(), type, path);
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
            addMidiEvent(idx, startSample);
        });

        connect(row.view, &TrackViewWidget::dragInProgress, this,
                [this, srcIdx = trackIndex]
                (int64_t eventId, int64_t currentStartSample, QPoint globalPos) {
            updateDragPreviews(srcIdx, eventId, currentStartSample, globalPos);
        });

        connect(row.view, &TrackViewWidget::eventDragFinished, this,
                [this, srcIdx = trackIndex]
                (int64_t eventId, int64_t oldStart, int64_t newStart,
                 QPoint globalPos, bool wasDuplicate) {
            finishEventDrag(srcIdx, eventId, oldStart, newStart, globalPos, wasDuplicate);
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

        row.innerSplitter = new QSplitter(Qt::Horizontal, row.row);
        row.innerSplitter->setContentsMargins(0, 0, 0, 0);
        row.innerSplitter->addWidget(row.pluginList);
        row.innerSplitter->addWidget(row.view);
        row.innerSplitter->setStretchFactor(0, 0);
        row.innerSplitter->setStretchFactor(1, 1);
        row.innerSplitter->setSizes({track.pluginPanelWidth(), 1000 - track.pluginPanelWidth()});

        int splitterIndex = static_cast<int>(m_trackSplitters.size());
        m_trackSplitters.push_back(row.innerSplitter);

        connect(row.innerSplitter, &QSplitter::splitterMoved, this,
                [this, splitterIndex](int pos, int index) {
            Q_UNUSED(index);
            Q_UNUSED(pos);
            syncPluginListSplitters(splitterIndex);
        });

        row.row->assemble(row.panel, row.innerSplitter);
        row.row->applyHeight(track.height());

        m_trackLayout->addWidget(row.row);

        m_trackRows.push_back(row);
        wireTrackRowGestures(row.row);
}

void MainWindow::wireTrackRowGestures(TrackRowWidget* row) {
        connect(row, &TrackRowWidget::resizeStarted, this, [this](int, int) {
            m_resizeStartHeights.clear();
            for (const auto& t : m_project.tracks())
                m_resizeStartHeights.push_back(t.height());
        });

        connect(row, &TrackRowWidget::resizeDragged, this,
                [this](int index, int newHeight, bool all) {
            if (all) {
                int common = qBound(maxTrackRowMinHeight(), newHeight, vvvdaw::MaxTrackHeight);
                for (int i = 0; i < static_cast<int>(m_trackRows.size()); ++i) {
                    if (i < static_cast<int>(m_project.tracks().size()))
                        m_project.tracks()[i].setHeight(common);
                    if (m_trackRows[i].row)
                        m_trackRows[i].row->applyHeight(common);
                }
            } else {
                applyTrackHeight(index, newHeight);
            }
        });

        connect(row, &TrackRowWidget::resizeFinished, this,
                [this](int index, int oldHeight, int newHeight, bool all) {
            if (all) {
                std::vector<int> oldHeights = m_resizeStartHeights;
                if (oldHeights.empty())
                    for (const auto& t : m_project.tracks()) oldHeights.push_back(t.height());
                pushCommand(std::make_unique<SetAllTracksHeightCommand>(m_project, oldHeights, newHeight));
            } else {
                int oldH = oldHeight;
                if (index >= 0 && index < static_cast<int>(m_resizeStartHeights.size()))
                    oldH = m_resizeStartHeights[index];
                int curH = (index >= 0 && index < static_cast<int>(m_project.tracks().size()))
                               ? m_project.tracks()[index].height() : oldH;
                if (oldH == curH) return;
                pushCommand(std::make_unique<SetTrackHeightCommand>(m_project, index, oldH, curH));
            }
        });

        connect(row, &TrackRowWidget::reorderDragStarted, this,
                [this](int index) { m_trackReorderSource = index; });

        connect(row, &TrackRowWidget::reorderDragMoved, this,
                [this](int, QPoint globalPos) {
            updateTrackInsertionLine(trackInsertionIndexAt(globalPos));
        });

        connect(row, &TrackRowWidget::reorderDragFinished, this,
                [this](int index, QPoint globalPos) {
            hideTrackInsertionLine();
            const int n = static_cast<int>(m_project.tracks().size());
            const int src = m_trackReorderSource;
            m_trackReorderSource = -1;
            if (n < 2 || src < 0 || src >= n) return;
            const int dst = trackInsertionIndexAt(globalPos);
            std::vector<int> oldOrder(static_cast<size_t>(n));
            std::iota(oldOrder.begin(), oldOrder.end(), 0);
            std::vector<int> newOrder = moveTrackOrder(oldOrder, src, dst);
            if (newOrder == oldOrder) return;
            executeCommand(std::make_unique<ReorderTracksCommand>(m_project, newOrder));
        });
}

int MainWindow::trackInsertionIndexAt(const QPoint& globalPos) const {
    QPoint local = m_trackContainer->mapFromGlobal(globalPos);
    int index = 0;
    for (const auto& row : m_trackRows) {
        if (row.row && local.y() > row.row->geometry().center().y())
            ++index;
    }
    return qBound(0, index, static_cast<int>(m_trackRows.size()));
}

void MainWindow::updateTrackInsertionLine(int insertionIndex) {
    if (!m_trackInsertionLine) {
        m_trackInsertionLine = new QFrame(m_trackContainer);
        m_trackInsertionLine->setObjectName("trackInsertionLine");
        m_trackInsertionLine->setFixedHeight(2);
        m_trackInsertionLine->setStyleSheet("QFrame { background: #4488cc; border: none; }");
    }
    const int n = static_cast<int>(m_trackRows.size());
    if (n == 0) {
        m_trackInsertionLine->hide();
        return;
    }
    if (insertionIndex < 0) insertionIndex = 0;
    if (insertionIndex > n) insertionIndex = n;
    int y = 0;
    if (insertionIndex < n && m_trackRows[insertionIndex].row) {
        y = m_trackRows[insertionIndex].row->geometry().top() - 1;
    } else if (m_trackRows.back().row) {
        y = m_trackRows.back().row->geometry().bottom() - 1;
    }
    m_trackInsertionLine->setGeometry(0, qMax(0, y), qMax(1, m_trackContainer->width()), 2);
    m_trackInsertionLine->show();
    m_trackInsertionLine->raise();
}

void MainWindow::hideTrackInsertionLine() {
    if (m_trackInsertionLine)
        m_trackInsertionLine->hide();
}

int MainWindow::maxTrackRowMinHeight() const {
    int maxMin = vvvdaw::TrackResizeHandleHeight + 1;
    for (const auto& row : m_trackRows) {
        if (row.row)
            maxMin = qMax(maxMin, row.row->minimumRowHeight());
    }
    return maxMin;
}

void MainWindow::applyTrackHeight(int index, int height) {
    if (index < 0 || index >= static_cast<int>(m_project.tracks().size())) return;
    m_project.tracks()[index].setHeight(height);
    if (index < static_cast<int>(m_trackRows.size()) && m_trackRows[index].row)
        m_trackRows[index].row->applyHeight(height);
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

    // Align the ruler spacers with the (shared) effects-panel width.
    if (!m_project.tracks().empty())
        updateRulerSpacers(200 + m_project.tracks().front().pluginPanelWidth());

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

void MainWindow::addMidiEvent(int trackIndex, int64_t startSample) {
    QJsonObject clipJson;
    clipJson["ppq"] = MidiClip::kPPQ;
    clipJson["lengthTicks"] = static_cast<qint64>(MidiClip::kPPQ) * 4;
    QJsonObject eventJson;
    eventJson["startSample"] = static_cast<qint64>(startSample);
    eventJson["offsetSample"] = 0;
    eventJson["durationSample"] = static_cast<qint64>(m_project.samplesPerBar());
    eventJson["clip"] = clipJson;
    auto cmd = std::make_unique<AddMidiEventCommand>(m_project, trackIndex, eventJson);
    executeCommand(std::move(cmd));
    if (auto* added = dynamic_cast<AddMidiEventCommand*>(m_undoStack.topCommand()))
        if (added->createdEventId() >= 0)
            openPianoRoll(trackIndex, added->createdEventId());
}

void MainWindow::updateDragPreviews(int srcIdx, int64_t eventId, int64_t currentStartSample,
                                    QPoint globalPos) {
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
}

void MainWindow::finishEventDrag(int srcIdx, int64_t eventId, int64_t oldStart,
                                 int64_t newStart, QPoint globalPos, bool wasDuplicate) {
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
}
