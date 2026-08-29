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

void MainWindow::closeAllPluginWindows() {
    std::vector<PluginWindow*> toClose = m_pluginWindows;
    for (auto* w : toClose)
        w->close();
    std::vector<PluginInstance*> detached(m_detachedEditors.begin(), m_detachedEditors.end());
    closeDetachedEditorsFor(detached);
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
    closeDetachedEditorsFor(plugins);
}


void MainWindow::closeDetachedEditorsFor(const std::vector<PluginInstance*>& plugins) {
    for (auto* p : plugins) {
        auto it = m_detachedEditors.find(p);
        if (it == m_detachedEditors.end()) continue;
        m_detachedEditors.erase(it);
        auto* lv2 = dynamic_cast<LV2Instance*>(p);
        if (!lv2) continue;
        lv2->setEditorClosedCallback({});
        lv2->destroyEditor();
        lv2->setParameterChangeCallback({});
        lv2->setStringParameterChangeCallback({});
        updateEditorRenderCount();
    }
}


void MainWindow::openDetachedPluginEditor(LV2Instance* lv2, PluginChain* chain) {
    if (lv2->editorOpen()) {
        if (lv2->externalEditorWindow() == 0)
            return; // toplevel not discovered yet (still mapping)
        if (lv2->raiseExternalEditor())
            return;
        // The window is gone but the host state lingers (the UI process may
        // hide instead of exiting, so closing was not detected): rebuild.
        closeDetachedEditorsFor({lv2});
    }
    if (!lv2->createEditor(this)) {
        qWarning() << "Could not open detached editor for" << lv2->name();
        return;
    }
    m_detachedEditors.insert(lv2);
    lv2->setEditorClosedCallback([this, lv2]() {
        m_detachedEditors.erase(lv2);
        lv2->setParameterChangeCallback({});
        lv2->setStringParameterChangeCallback({});
        updateEditorRenderCount();
    });
    lv2->setParameterChangeCallback(
        [this, chain, lv2](int paramIndex, float oldValue, float newValue) {
        if (chain)
            m_undoStack.execute(
                std::make_unique<SetPluginParameterCommand>(*chain, lv2, paramIndex, oldValue, newValue));
    });
    lv2->setStringParameterChangeCallback(
        [this, chain, lv2](int paramIndex, const QString& oldValue, const QString& newValue) {
        if (chain && oldValue != newValue)
            m_undoStack.execute(
                std::make_unique<SetPluginPathParameterCommand>(*chain, lv2, paramIndex, oldValue, newValue));
    });
    updateEditorRenderCount();
}

// While any plugin editor is open the engine keeps rendering plugins with
// silent blocks even when the transport is stopped, so UI<->plugin state
// transfer works without starting playback.
void MainWindow::updateEditorRenderCount() {
    int count = static_cast<int>(m_detachedEditors.size());
    for (auto* w : m_pluginWindows)
        if (w->isVisible())
            ++count;
    m_engine.setEditorsOpen(count);
}


void MainWindow::closePluginWindowsFor(PluginInstance* plugin) {
    closePluginWindowsFor(std::vector<PluginInstance*>{plugin});
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

    // Separate-window native UIs (DPF ExternalWindow, e.g. ZynAddSubFX) run
    // detached as their own toplevel; opening an empty PluginWindow for them
    // would just add a stray blank window.
    auto* lv2 = dynamic_cast<LV2Instance*>(plugin);
    if (lv2 && lv2->hasNativeUI() && lv2->isNativeUISeparateWindow()) {
        openDetachedPluginEditor(lv2, chain);
        return;
    }

    // Embedded native X11 UIs: LV2Instance::createEditor reparents into
    // the PluginWindow below.

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
        updateEditorRenderCount();
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
    updateEditorRenderCount();
}
