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

void MainWindow::setupBusPanel(QVBoxLayout* layout) {
    // Bus panel grip (draggable resize handle)
    m_busPanelGrip = makePanelGrip(this);
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
        addPluginToChain(m_project.buses()[busIndex].pluginChain(), type, path);
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
    m_instrumentPanelGrip = makePanelGrip(this);
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

    connect(m_instrumentPanel, &InstrumentPanelWidget::addInstrumentRequested, this,
            [this](const QString& type, const QString& path) {
        QJsonObject synthJson;
        synthJson["type"] = type;
        synthJson["path"] = path;
        executeCommand(std::make_unique<AddInstrumentCommand>(
            m_project, &m_pluginManager, m_engine.sampleRate(),
            m_engine.bufferSize(), synthJson));
        // The new instrument is the last one; open its synth editor right away.
        if (!m_project.instruments().empty()) {
            if (auto* synth = m_project.instruments().back().synth())
                openPluginEditor(synth);
        }
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
        addPluginToChain(m_project.instruments()[index].effects(), type, path);
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


void MainWindow::refreshBusCombos() {
    std::vector<std::pair<int, QString>> midiOutList;
    std::vector<QString> instrumentNames;
    collectDeviceLists(midiOutList, instrumentNames);
    for (auto& row : m_trackRows) {
        if (row.panel) {
            row.panel->updateBusList(m_project.buses());
            row.panel->updateMidiOutputs(midiOutList, instrumentNames);
        }
    }
}

QWidget* MainWindow::makePanelGrip(QWidget* parent) {
    auto* grip = new QWidget(parent);
    grip->setFixedHeight(6);
    grip->setCursor(Qt::SizeVerCursor);
    grip->setStyleSheet("background-color: #555;:hover { background-color: #777; }");
    return grip;
}

void MainWindow::addPluginToChain(PluginChain& chain, const QString& type, const QString& path) {
    QJsonObject pluginJson;
    pluginJson["type"] = type;
    pluginJson["path"] = path;
    auto cmd = std::make_unique<AddPluginCommand>(
        chain, pluginJson, &m_pluginManager,
        static_cast<double>(m_engine.sampleRate()), m_engine.bufferSize());
    cmd->setBeforeRemoveCallback([this](PluginInstance* plugin) {
        closePluginWindowsFor(plugin);
    });
    executeCommand(std::move(cmd));
    if (auto* added = dynamic_cast<AddPluginCommand*>(m_undoStack.topCommand()))
        if (added->addedPlugin()) openPluginEditor(added->addedPlugin());
}

void MainWindow::collectDeviceLists(std::vector<std::pair<int, QString>>& midiOutList,
                                    std::vector<QString>& instrumentNames) {
    auto midiDevices = AudioEngine::enumerateMidiOutputDevices();
    midiOutList.clear();
    for (const auto& dev : midiDevices)
        midiOutList.emplace_back(dev.id, dev.name);
    instrumentNames.clear();
    for (const auto& inst : m_project.instruments())
        instrumentNames.push_back(inst.name());
}
