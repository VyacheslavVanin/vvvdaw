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

void MainWindow::setupRulerConnections() {
    auto onPlayheadClicked = [this](int64_t sample) {
        m_engine.setPlayPosition(sample);
        syncPlayheadViews(sample);
        m_transportPanel->setTimeText(TimeUtils::formatTime(sample, m_engine.sampleRate()));
    };
    connect(m_timelineRuler, &TimelineRuler::playheadClicked, this, onPlayheadClicked);
    connect(m_measureRuler, &MeasureRuler::playheadClicked, this, onPlayheadClicked);

    // Loop signals (created/changed share one handler)
    auto onLoop = [this](int64_t start, int64_t end) {
        m_project.setLoop(start, end);
        m_timelineRuler->setLoop(start, end);
        m_measureRuler->setLoop(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::loopCreated, this, onLoop);
    connect(m_measureRuler, &MeasureRuler::loopCreated, this, onLoop);
    connect(m_timelineRuler, &TimelineRuler::loopChanged, this, onLoop);
    connect(m_measureRuler, &MeasureRuler::loopChanged, this, onLoop);

    auto onLoopRemoved = [this] {
        m_project.clearLoop();
        m_timelineRuler->clearLoop();
        m_measureRuler->clearLoop();
    };
    connect(m_timelineRuler, &TimelineRuler::loopRemoved, this, onLoopRemoved);
    connect(m_measureRuler, &MeasureRuler::loopRemoved, this, onLoopRemoved);

    // Record region signals (created/changed share one handler)
    auto onRecordRegion = [this](int64_t start, int64_t end) {
        m_project.setRecordRegion(start, end);
        m_timelineRuler->setRecordRegion(start, end);
        m_measureRuler->setRecordRegion(start, end);
    };
    connect(m_timelineRuler, &TimelineRuler::recordRegionCreated, this, onRecordRegion);
    connect(m_measureRuler, &MeasureRuler::recordRegionCreated, this, onRecordRegion);
    connect(m_timelineRuler, &TimelineRuler::recordRegionChanged, this, onRecordRegion);
    connect(m_measureRuler, &MeasureRuler::recordRegionChanged, this, onRecordRegion);

    auto onRecordRegionRemoved = [this] {
        m_project.clearRecordRegion();
        m_timelineRuler->clearRecordRegion();
        m_measureRuler->clearRecordRegion();
    };
    connect(m_timelineRuler, &TimelineRuler::recordRegionRemoved, this, onRecordRegionRemoved);
    connect(m_measureRuler, &MeasureRuler::recordRegionRemoved, this, onRecordRegionRemoved);

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
