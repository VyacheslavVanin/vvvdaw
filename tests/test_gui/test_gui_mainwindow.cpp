#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QPixmap>
#include <QSignalSpy>
#include <QTimer>
#include <QContextMenuEvent>
#include <QMimeData>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QFrame>
#include <QDialog>
#include <QWheelEvent>
#include <QScrollBar>
#include <QSplitter>
#include <algorithm>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <portaudio.h>

#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "audio/AudioUtils.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include "gui/MainWindow.h"
#include "gui/StartDialog.h"
#include "gui/TrackPanelWidget.h"
#include "gui/PanSlider.h"
#include "gui/TrackViewWidget.h"
#include "gui/WaveformPainter.h"
#include "gui/TimelineRuler.h"
#include "gui/MeasureRuler.h"
#include "gui/BusPanelWidget.h"
#include "gui/BusSendsWidget.h"
#include "gui/BusLevelMeter.h"
#include "gui/BusColorBar.h"
#include "gui/InstrumentPanelWidget.h"
#include "gui/PluginListWidget.h"
#include "gui/TrackRowWidget.h"
#include "gui/PluginWindow.h"
#include "gui/TrackColorBar.h"
#include "gui/PianoRollWindow.h"
#include "gui/PianoRollWidget.h"
#include "gui/ChannelRoutingDialog.h"
#include "gui/SettingsDialog.h"
#include "commands/TrackCommands.h"
#include "commands/SnapshotCommand.h"
#include "GuiTestHelpers.h"

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void constructEmptyProject();
    void constructWithTracks();
    void rebuildAfterTrackChanges();
    void rebuildWithBusesAndInstruments();
    void addTrackViaSignal();
    void trackRowsApplyStoredHeight();
    void resizeTrackUpdatesHeightAndRow();
    void resizeAllTracksViaRowSignals();
    void scaleTrackHeightsMath();
    void panelCollapsesControlRows();
    void minimumRowHeightFitsNameRow();
    void trackPluginPanelWidthAppliesToRow();
    void splitterSyncPersistsPanelWidth();
    void reorderTracksViaCommand();
    void audioTrackOutComboListsBuses();
    void instrumentOutComboShowsMultiChannel();
    void channelRoutingDialogCreatesBuses();
    void mainWindowRestoresPanelStateFromSettings();
    void mainWindowRestoresSizeFromSettings();
    void panelTogglesAndGripUpdateSettings();
    void busRenameRefreshesTrackOutCombo();
    void instrumentRenameRefreshesMidiTrackOutCombo();
    void busRenameRefreshesBusAndInstrumentOutCombos();
    void rejectAudioEventToMidiTrack();
    void rejectMidiEventToAudioTrack();
    void startDialogListsRecentProjects();
    void startDialogListsTemplates();
    void startDialogSelectingTemplateSetsChoice();
    void startDialogDeleteTemplateRemovesFromList();
    void startDialogDeleteBuiltinTemplateRefused();
    void startDialogDeleteRecentRemovesFromList();
    void mainWindowFileMenuHasSaveAsTemplate();
    void replaceProjectSwapsAndRebuilds();
    void midiTrackShowsArmButton();
    void settingsDialogHasMidiInputControls();
    void settingsDialogLearnFlow();
    void executeCommandAcquiresProjectWriteLock();
    void undoRedoAcquireProjectWriteLock();
    void trackColorBarAssignsAndResets();
    void trackColorTintsPanelTimelineAndEvents();
    void trackColorPaletteUsesTrackScheme();
    void trackRowTintKeepsPanelSubtle();
    void newTrackInheritsPanelAndHeight();
private:
    GuiTestEnv m_env;
};

void MainWindowTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void MainWindowTest::cleanupTestCase() {
    m_env.cleanup();
}

void MainWindowTest::constructEmptyProject() {
    Project project;
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.windowTitle().contains(project.name()));
    QVERIFY(window.m_trackRows.empty());
    QVERIFY(window.findChildren<TrackPanelWidget*>().isEmpty());
    QVERIFY(window.findChild<TimelineRuler*>());
    QVERIFY(window.findChild<MeasureRuler*>());
    QVERIFY(window.findChild<BusPanelWidget*>());
    QVERIFY(window.findChild<InstrumentPanelWidget*>());
}


void MainWindowTest::constructWithTracks() {
    Project project;
    project.addTrack("Audio 1");
    project.addTrack("Audio 2", 1);
    project.addMidiTrack("Midi 1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QCOMPARE(window.m_trackRows.size(), size_t(3));
    QCOMPARE(window.findChildren<TrackPanelWidget*>().size(), 3);
    QCOMPARE(window.findChildren<TrackViewWidget*>().size(), 3);
}


void MainWindowTest::rebuildAfterTrackChanges() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    project.addTrack("T2");
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    project.removeTrack(0);
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    project.addMidiTrack("Midi");
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(2));
}


void MainWindowTest::rebuildWithBusesAndInstruments() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    AudioBus bus;
    bus.setName("FX");
    project.addBus(std::move(bus));
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    window.rebuildTracks();
    // Buses and instruments do not create track rows.
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    QVERIFY(window.findChild<BusPanelWidget*>());
    QVERIFY(window.findChild<InstrumentPanelWidget*>());
}


void MainWindowTest::addTrackViaSignal() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    auto panels = window.findChildren<TrackPanelWidget*>();
    QCOMPARE(panels.size(), 1);
    emit panels[0]->addTrackRequested(2);

    // The command pipeline rebuilds the rows synchronously; flush any
    // deferred widget deletions from the rebuild before counting.
    QCoreApplication::processEvents();
    QCOMPARE(window.m_trackRows.size(), size_t(2));
    QCOMPARE(window.m_project.tracks().size(), size_t(2));
}


void MainWindowTest::trackRowsApplyStoredHeight() {
    Project project;
    project.addTrack("A");
    project.addMidiTrack("B");
    project.tracks()[0].setHeight(200);
    project.tracks()[1].setHeight(90);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    QVERIFY(window.m_trackRows[0].row);
    QVERIFY(window.m_trackRows[1].row);
    QCOMPARE(window.m_trackRows[0].row->rowHeight(), 200);
    QCOMPARE(window.m_trackRows[1].row->rowHeight(), 90);
}


void MainWindowTest::resizeTrackUpdatesHeightAndRow() {
    Project project;
    project.addTrack("A");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    const int original = window.m_project.tracks()[0].height();

    window.applyTrackHeight(0, 300);
    QCOMPARE(window.m_project.tracks()[0].height(), 300);
    QCOMPARE(window.m_trackRows[0].row->rowHeight(), 300);

    // The signal handler pushes a height command; undo restores the original.
    window.pushCommand(std::make_unique<SetTrackHeightCommand>(
        window.m_project, 0, original, 300));
    window.performUndo();
    QCOMPARE(window.m_project.tracks()[0].height(), original);
    QCOMPARE(window.m_trackRows[0].row->rowHeight(), original);
}


void MainWindowTest::resizeAllTracksViaRowSignals() {
    Project project;
    project.addTrack("A");
    project.addTrack("B");
    project.addTrack("C");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(3));

    // Non-uniform start heights: Shift-drag scales them proportionally.
    window.applyTrackHeight(0, 160);
    window.applyTrackHeight(1, 200);
    window.applyTrackHeight(2, 240);

    window.show();
    window.resize(1000, 900);
    QCoreApplication::processEvents();

    TrackRowWidget* row = window.m_trackRows[0].row;
    QVERIFY(row);

    // Press on the bottom edge of row 0 (container y=160) and drag it to
    // y=240: the grabbed handle follows the cursor and the scale factor is
    // 240/160 = 1.5 → heights (240, 300, 360).
    const QPoint pressGlobal = window.m_trackContainer->mapToGlobal(QPoint(10, 160));
    emit row->resizeStarted(0, 160, pressGlobal);
    const QPoint dragGlobal = window.m_trackContainer->mapToGlobal(QPoint(10, 240));
    emit row->resizeDragged(0, 240, dragGlobal, true);

    QCOMPARE(window.m_project.tracks()[0].height(), 240);
    QCOMPARE(window.m_project.tracks()[1].height(), 300);
    QCOMPARE(window.m_project.tracks()[2].height(), 360);
    QCOMPARE(window.m_trackRows[0].row->rowHeight(), 240);
    QCOMPARE(window.m_trackRows[1].row->rowHeight(), 300);
    QCOMPARE(window.m_trackRows[2].row->rowHeight(), 360);

    emit row->resizeFinished(0, 160, 240, true);
    window.performUndo();
    QCoreApplication::processEvents();
    QCOMPARE(window.m_project.tracks()[0].height(), 160);
    QCOMPARE(window.m_project.tracks()[1].height(), 200);
    QCOMPARE(window.m_project.tracks()[2].height(), 240);
}


void MainWindowTest::scaleTrackHeightsMath() {
    // Scale 1.5 keeps the proportions.
    {
        std::vector<int> out = scaleTrackHeights({160, 200, 240}, 600, 900);
        QCOMPARE(out.size(), size_t(3));
        QCOMPARE(out[0], 240);
        QCOMPARE(out[1], 300);
        QCOMPARE(out[2], 360);
    }
    // Rounding to nearest (no clamp involved).
    {
        std::vector<int> out = scaleTrackHeights({100, 51}, 151, 300);
        QCOMPARE(out.size(), size_t(2));
        QCOMPARE(out[0], 199);
        QCOMPARE(out[1], 101);
    }
    // Upper clamp at MaxTrackHeight.
    QCOMPARE(scaleTrackHeights({100}, 100, 100000)[0], vvvdaw::MaxTrackHeight);
    // Lower clamp at the handle floor when the target is above the top edge.
    QCOMPARE(scaleTrackHeights({100}, 100, -50)[0], vvvdaw::TrackResizeHandleHeight + 1);
    // Non-positive pressBottom guard returns the start heights unchanged.
    std::vector<int> start = {100, 200};
    std::vector<int> out = scaleTrackHeights(start, 0, 300);
    QCOMPARE(out.size(), size_t(2));
    QCOMPARE(out[0], 100);
    QCOMPARE(out[1], 200);
}


void MainWindowTest::panelCollapsesControlRows() {
    Project project;
    project.addTrack("A", 2);
    project.addMidiTrack("B");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    TrackPanelWidget* audio = window.m_trackRows[0].panel;
    TrackPanelWidget* midi = window.m_trackRows[1].panel;

    // Audio has three control rows; MIDI only the out row.
    QCOMPARE(audio->visibleControlRowCount(), 3);
    QCOMPARE(midi->visibleControlRowCount(), 1);

    // Collapse to only the name row.
    audio->applyContentHeight(audio->nameRowHeight());
    midi->applyContentHeight(midi->nameRowHeight());
    QCOMPARE(audio->visibleControlRowCount(), 0);
    QCOMPARE(midi->visibleControlRowCount(), 0);

    // Expanding back shows them again.
    audio->applyContentHeight(audio->fullContentHeight());
    midi->applyContentHeight(midi->fullContentHeight());
    QCOMPARE(audio->visibleControlRowCount(), 3);
    QCOMPARE(midi->visibleControlRowCount(), 1);
}


void MainWindowTest::minimumRowHeightFitsNameRow() {
    Project project;
    project.addTrack("A", 2);
    project.addMidiTrack("B");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    for (size_t i = 0; i < window.m_trackRows.size(); ++i) {
        TrackRowWidget* row = window.m_trackRows[i].row;
        TrackPanelWidget* panel = window.m_trackRows[i].panel;
        QVERIFY(row);
        QVERIFY(panel);

        // The minimum row height must fit the whole name row (buttons included),
        // so it accounts for the panel's vertical margins + the resize handle.
        QCOMPARE(row->minimumRowHeight(),
                 panel->minimumContentHeight() + vvvdaw::TrackResizeHandleHeight);
        QVERIFY(panel->minimumContentHeight() > panel->nameRowHeight());

        // At the minimum the panel stays fully collapsed (name row only).
        row->applyHeight(row->minimumRowHeight());
        QCOMPARE(panel->visibleControlRowCount(), 0);
    }
}


void MainWindowTest::trackPluginPanelWidthAppliesToRow() {
    Project project;
    project.addTrack("A", 2);
    project.tracks()[0].setPluginPanelWidth(320);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    QVERIFY(window.m_trackRows[0].innerSplitter);
    window.show();
    QCoreApplication::processEvents();
    QCOMPARE(window.m_trackRows[0].innerSplitter->sizes().value(0), 320);
}


void MainWindowTest::splitterSyncPersistsPanelWidth() {
    Project project;
    project.addTrack("A", 2);
    project.addMidiTrack("B");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    QVERIFY(window.m_trackRows[0].innerSplitter);
    QVERIFY(window.m_trackRows[1].innerSplitter);
    window.show();
    QCoreApplication::processEvents();

    window.m_trackRows[0].innerSplitter->setSizes({280, 920});
    const int actual = window.m_trackRows[0].innerSplitter->sizes().value(0);
    window.syncPluginListSplitters(0);

    // Dragging one effects panel keeps all tracks in sync and persists the
    // width into the model so it can be saved with the project.
    QCOMPARE(window.m_project.tracks()[0].pluginPanelWidth(), actual);
    QCOMPARE(window.m_project.tracks()[1].pluginPanelWidth(), actual);
}


void MainWindowTest::reorderTracksViaCommand() {
    Project project;
    project.addTrack("A");
    project.addTrack("B");
    project.addTrack("C");
    project.addTrack("D");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(4));

    // Move track 0 ("A") to before position 2.
    window.executeCommand(std::make_unique<ReorderTracksCommand>(window.m_project,
        std::vector<int>{1, 0, 2, 3}));
    QCoreApplication::processEvents();
    QCOMPARE(window.m_project.tracks()[0].name(), QString("B"));
    QCOMPARE(window.m_project.tracks()[1].name(), QString("A"));
    QCOMPARE(window.m_trackRows.size(), size_t(4));
    QCOMPARE(window.m_trackRows[0].panel->track()->name(), QString("B"));
    QCOMPARE(window.m_trackRows[1].panel->track()->name(), QString("A"));

    window.performUndo();
    QCoreApplication::processEvents();
    QCOMPARE(window.m_project.tracks()[0].name(), QString("A"));
    QCOMPARE(window.m_project.tracks()[3].name(), QString("D"));
    QCOMPARE(window.m_trackRows[0].panel->track()->name(), QString("A"));
}


void MainWindowTest::audioTrackOutComboListsBuses() {
    Project project;
    project.addTrack("Audio 1");
    project.addMidiTrack("Midi 1");
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    QComboBox* audioOut = findComboContaining(window.m_trackRows[0].panel, "Master");
    QVERIFY(audioOut);
    QCOMPARE(audioOut->count(), static_cast<int>(project.buses().size()));
    for (int i = 0; i < audioOut->count(); ++i) {
        QVERIFY(!audioOut->itemText(i).contains("Inst:"));
        QVERIFY(!audioOut->itemText(i).contains("MIDI:"));
    }

    QComboBox* midiOut = findComboContaining(window.m_trackRows[1].panel, "Inst:");
    QVERIFY(midiOut);
    bool foundInst = false;
    for (int i = 0; i < midiOut->count(); ++i) {
        if (midiOut->itemText(i).contains("Inst: Pad"))
            foundInst = true;
        // The MIDI out combo must not list any project bus as an item (checked
        // by exact name: a MIDI output device may legitimately contain "Master"
        // as a substring in its own name).
        for (const auto& bus : project.buses())
            QVERIFY(midiOut->itemText(i) != bus.name());
    }
    QVERIFY(foundInst);
}


void MainWindowTest::instrumentOutComboShowsMultiChannel() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    inst.setOutputBusIndex(1);
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_instrumentPanel->rebuild();

    auto* panel = window.m_instrumentPanel;

    // The out combo offers a "Multi Channel" entry; single-bus instruments
    // keep the plain bus selection.
    QComboBox* out = nullptr;
    for (QComboBox* cb : panel->findChildren<QComboBox*>()) {
        bool hasMulti = false;
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toInt() == -1 && cb->itemText(i) == "Multi Channel")
                hasMulti = true;
        }
        if (hasMulti) { out = cb; break; }
    }
    QVERIFY(out);
    QCOMPARE(out->currentData().toInt(), 1); // routed to bus 1, not multi

    // An instrument in multi-channel mode selects the "Multi Channel" entry.
    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r;
    r.busIndex = 0;
    r.name = "Ch0";
    routes.push_back(r);
    project.instruments()[0].setMultiChannel(true);
    project.instruments()[0].setChannelRoutes(routes);
    window.m_instrumentPanel->rebuild();

    QComboBox* multiOut = nullptr;
    for (QComboBox* cb : panel->findChildren<QComboBox*>()) {
        if (cb->currentData().toInt() == -1) {
            multiOut = cb;
            break;
        }
    }
    QVERIFY(multiOut);
    QCOMPARE(multiOut->currentText(), QString("Multi Channel"));
}


void MainWindowTest::channelRoutingDialogCreatesBuses() {
    Project project;
    Instrument inst;
    inst.setName("Drums");
    auto synth = std::make_unique<StubSynth>();
    synth->channels = 3;
    inst.setSynth(std::move(synth));
    project.addInstrument(std::move(inst));

    const int busCountBefore = static_cast<int>(project.buses().size()); // 2

    ChannelRoutingDialog dialog(project, project.instruments()[0],
                                project.instruments()[0].synth());

    auto* createBtn = dialog.findChild<QPushButton*>("createBusesButton");
    QVERIFY(createBtn);
    createBtn->click();

    // One new bus per channel, each channel assigned to its own bus.
    QCOMPARE(project.buses().size(), size_t(busCountBefore + 3));
    QCOMPARE(dialog.createdBusCount(), 3);

    auto* table = dialog.findChild<QTableWidget*>();
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 3);
    for (int c = 0; c < 3; ++c) {
        auto* combo = qobject_cast<QComboBox*>(table->cellWidget(c, 1));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), busCountBefore + c);
    }

    // Buses are named after the (default) channel names.
    QCOMPARE(project.buses()[busCountBefore].name(), QString("Kick"));
    QCOMPARE(project.buses()[busCountBefore + 1].name(), QString("Snare"));
    QCOMPARE(project.buses()[busCountBefore + 2].name(), QString("HiHat"));

    // Rejecting the dialog rolls the created buses back out of the project.
    dialog.reject();
    QCOMPARE(project.buses().size(), size_t(busCountBefore));
    QCOMPARE(dialog.createdBusCount(), 0);
}


void MainWindowTest::mainWindowRestoresPanelStateFromSettings() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    settings.busPanelVisible = true;
    settings.busPanelHeight = 320;
    settings.instrumentPanelVisible = true;
    settings.instrumentPanelHeight = 260;

    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.m_busPanel->isHidden());
    QCOMPARE(window.m_busPanel->maximumHeight(), 320);
    QVERIFY(!window.m_instrumentPanel->isHidden());
    QCOMPARE(window.m_instrumentPanel->maximumHeight(), 260);
    QVERIFY(!window.m_busPanelGrip->isHidden());
    QVERIFY(!window.m_instrumentPanelGrip->isHidden());

    // The restored panels must actually contain their widgets (rows built
    // during construction), not render empty until toggled.
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("soloButton").size(),
             project.buses().size());
    bool foundSynthButton = false;
    for (QPushButton* b : window.m_instrumentPanel->findChildren<QPushButton*>())
        if (b->text() == "No Synth") foundSynthButton = true;
    QVERIFY(foundSynthButton);

    // The View menu checkmarks reflect the restored visibility.
    bool busChecked = false, instChecked = false;
    for (auto* menuAction : window.menuBar()->actions()) {
        auto* menu = menuAction->menu();
        if (!menu) continue;
        for (auto* action : menu->actions()) {
            if (action->text().contains("Bus Panel")) busChecked = action->isChecked();
            if (action->text().contains("Instrument Panel")) instChecked = action->isChecked();
        }
    }
    QVERIFY(busChecked);
    QVERIFY(instChecked);
}


void MainWindowTest::mainWindowRestoresSizeFromSettings() {
    Project project;
    Settings settings;
    settings.mainWindowWidth = 1100;
    settings.mainWindowHeight = 650;

    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QCOMPARE(window.size().width(), 1100);
    QCOMPARE(window.size().height(), 650);
}


void MainWindowTest::panelTogglesAndGripUpdateSettings() {
    Project project;
    Settings settings; // both panels hidden, default heights
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.m_busPanel->isHidden());
    QVERIFY(!settings.busPanelVisible);

    QAction* busAction = nullptr;
    for (auto* menuAction : window.menuBar()->actions()) {
        auto* menu = menuAction->menu();
        if (!menu) continue;
        for (auto* action : menu->actions()) {
            if (action->text().contains("Bus Panel"))
                busAction = action;
        }
    }
    QVERIFY(busAction);
    busAction->trigger();
    QVERIFY(settings.busPanelVisible);
    QVERIFY(!window.m_busPanel->isHidden());

    // Dragging the bus grip writes the new height into settings.
    const int startHeight = settings.busPanelHeight;
    window.show();
    window.resize(1000, 800);
    QCoreApplication::processEvents();

    QWidget* grip = window.m_busPanelGrip;
    QVERIFY(!grip->isHidden());
    const QPoint gripCenter = grip->rect().center();
    const QPoint above = gripCenter - QPoint(0, 40);

    QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, gripCenter);
    QTest::mouseMove(grip, above);
    QTest::mouseRelease(grip, Qt::LeftButton, Qt::NoModifier, above);
    QCoreApplication::processEvents();

    // The drag moved the handle upward, increasing the panel height.
    QVERIFY(settings.busPanelHeight > startHeight);
}


void MainWindowTest::busRenameRefreshesTrackOutCombo() {
    Project project;
    project.addTrack("Audio 1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QComboBox* audioOut = findComboContaining(window.m_trackRows[0].panel, "Metronome");
    QVERIFY(audioOut);

    window.m_project.buses()[1].setName("FX Bus");
    emit window.m_busPanel->busNameWillChange(1, "Metronome", "FX Bus");

    QVERIFY(findComboContaining(window.m_trackRows[0].panel, "FX Bus"));
    QVERIFY(!findComboContaining(window.m_trackRows[0].panel, "Metronome"));
}


void MainWindowTest::instrumentRenameRefreshesMidiTrackOutCombo() {
    Project project;
    project.addMidiTrack("Midi 1");
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));
    project.tracks()[0].setInstrumentIndex(0);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QComboBox* midiOut = findComboContaining(window.m_trackRows[0].panel, "Inst:");
    QVERIFY(midiOut);
    QVERIFY(midiOut->currentText().contains("Inst: Pad"));

    window.m_project.instruments()[0].setName("Lead");
    emit window.m_instrumentPanel->nameWillChange(0, "Pad", "Lead");

    midiOut = findComboContaining(window.m_trackRows[0].panel, "Inst: Lead");
    QVERIFY(midiOut);
    QVERIFY(midiOut->currentText().contains("Inst: Lead"));
}


void MainWindowTest::busRenameRefreshesBusAndInstrumentOutCombos() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    inst.setOutputBusIndex(1);
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    window.m_busPanel->rebuild();
    window.m_instrumentPanel->rebuild();

    QVERIFY(findComboContaining(window.m_busPanel, "Metronome"));
    QVERIFY(findComboContaining(window.m_instrumentPanel, "Metronome"));

    window.m_project.buses()[1].setName("FX Bus");
    emit window.m_busPanel->busNameWillChange(1, "Metronome", "FX Bus");

    QVERIFY(findComboContaining(window.m_busPanel, "FX Bus"));
    QVERIFY(findComboContaining(window.m_instrumentPanel, "FX Bus"));
}


void MainWindowTest::rejectAudioEventToMidiTrack() {
    Project project;
    project.addTrack("A1");
    project.addMidiTrack("M1");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    AudioEvent ev;
    ev.setStartSample(100);
    src.addEvent(ev);
    const int64_t id = src.events().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.moveEventToTrack(0, 1, id, 500));
    QCOMPARE(src.events().size(), size_t(1));
    QCOMPARE(src.events().front().startSample(), int64_t(100));
    QVERIFY(dst.events().empty());
    QVERIFY(dst.midiEvents().empty());
}


void MainWindowTest::rejectMidiEventToAudioTrack() {
    Project project;
    project.addMidiTrack("M1");
    project.addTrack("A1");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    MidiEvent ev;
    ev.setStartSample(100);
    src.addMidiEvent(ev);
    const int64_t id = src.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.moveEventToTrack(0, 1, id, 500));
    QCOMPARE(src.midiEvents().size(), size_t(1));
    QCOMPARE(src.midiEvents().front().startSample(), int64_t(100));
    QVERIFY(dst.events().empty());
    QVERIFY(dst.midiEvents().empty());
}


void MainWindowTest::startDialogListsRecentProjects() {
    const QString oldPath = m_env.filePath("project_old.json");
    const QString recentPath = m_env.filePath("project_recent.json");
    QFile oldFile(oldPath);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    oldFile.write("{}");
    QFile recentFile(recentPath);
    QVERIFY(recentFile.open(QIODevice::WriteOnly));
    recentFile.write("{}");

    Settings settings;
    settings.addRecentProject(oldPath);
    settings.addRecentProject(recentPath);

    StartDialog dialog(settings);
    QCOMPARE(dialog.m_recentList->count(), 2);
    QCOMPARE(dialog.m_recentList->item(0)->data(Qt::UserRole).toString(),
             recentPath);
    QCOMPARE(dialog.m_recentList->item(1)->data(Qt::UserRole).toString(),
             oldPath);
}


void MainWindowTest::startDialogListsTemplates() {
    Settings settings;
    StartDialog dialog(settings);
    QVERIFY(dialog.m_templateList->count() >= 2);
    QStringList texts;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        texts << dialog.m_templateList->item(i)->data(Qt::UserRole).toString();
    QVERIFY(texts.contains("empty"));
    QVERIFY(texts.contains("rock-band"));
}


void MainWindowTest::startDialogSelectingTemplateSetsChoice() {
    Settings settings;
    StartDialog dialog(settings);
    dialog.show();
    QCoreApplication::processEvents();

    int idx = -1;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        if (dialog.m_templateList->item(i)->data(Qt::UserRole).toString() == "empty")
            idx = i;
    QVERIFY(idx >= 0);
    dialog.m_templateList->setCurrentRow(idx);
    dialog.m_useTemplateButton->click();

    QCOMPARE(dialog.choice().action, StartDialog::Action::OpenTemplate);
    QCOMPARE(dialog.choice().templateName, QString("empty"));
}


void MainWindowTest::startDialogDeleteTemplateRemovesFromList() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    TemplateStore::setTemplatesDirOverride(tmp.path());

    Project source;
    source.addTrack("Drums");
    QVERIFY(TemplateStore::saveTemplate(source, "to_delete"));

    Settings settings;
    StartDialog dialog(settings);
    dialog.m_confirm = [](const QString&) { return true; };

    int idx = -1;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        if (dialog.m_templateList->item(i)->data(Qt::UserRole).toString() == "to_delete")
            idx = i;
    QVERIFY(idx >= 0);
    dialog.m_templateList->setCurrentRow(idx);
    dialog.m_deleteTemplateButton->click();

    QVERIFY(!TemplateStore::exists("to_delete"));
    bool found = false;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        if (dialog.m_templateList->item(i)->data(Qt::UserRole).toString() == "to_delete")
            found = true;
    QVERIFY(!found);

    TemplateStore::setTemplatesDirOverride("");
}


void MainWindowTest::startDialogDeleteBuiltinTemplateRefused() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    TemplateStore::setTemplatesDirOverride(tmp.path());
    TemplateStore::ensureBuiltInTemplates();

    Settings settings;
    StartDialog dialog(settings);
    dialog.m_confirm = [](const QString&) { return true; };

    int idx = -1;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        if (dialog.m_templateList->item(i)->data(Qt::UserRole).toString() == "empty")
            idx = i;
    QVERIFY(idx >= 0);
    dialog.m_templateList->setCurrentRow(idx);
    // Built-in templates cannot be deleted: the button is disabled.
    QVERIFY(!dialog.m_deleteTemplateButton->isEnabled());
    dialog.m_deleteTemplateButton->click();
    QVERIFY(TemplateStore::exists("empty"));

    TemplateStore::setTemplatesDirOverride("");
}


void MainWindowTest::startDialogDeleteRecentRemovesFromList() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Settings::setConfigDirOverride(tmp.path());

    const QString recentPath = m_env.filePath("project_delete.json");
    QFile f(recentPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();

    Settings settings;
    settings.addRecentProject(recentPath);
    StartDialog dialog(settings);
    dialog.m_confirm = [](const QString&) { return true; };

    QCOMPARE(dialog.m_recentList->count(), 1);
    dialog.m_recentList->setCurrentRow(0);
    dialog.m_deleteRecentButton->click();

    QCOMPARE(dialog.m_recentList->count(), 0);
    QVERIFY(settings.recentProjects().empty());
    QFile::remove(recentPath);

    Settings::setConfigDirOverride("");
}


void MainWindowTest::mainWindowFileMenuHasSaveAsTemplate() {
    Project project;
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    auto* fileMenu = window.menuBar()->actions().value(0)->menu();
    QVERIFY(fileMenu);
    bool found = false;
    for (auto* action : fileMenu->actions()) {
        if (action->text().contains("Template")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}


void MainWindowTest::replaceProjectSwapsAndRebuilds() {
    Project project;
    project.addTrack("T1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_project.tracks().size(), size_t(1));
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    Project fresh;
    fresh.addTrack("A");
    fresh.addTrack("B");
    const QString freshName = fresh.name();
    window.replaceProject(std::move(fresh));

    QCOMPARE(window.m_project.tracks().size(), size_t(2));
    QCOMPARE(window.m_project.tracks()[0].name(), QString("A"));
    QCOMPARE(window.m_project.tracks()[1].name(), QString("B"));
    QCOMPARE(window.m_project.name(), freshName);
    QCOMPARE(window.m_trackRows.size(), size_t(2));
    QVERIFY(window.m_project.filePath().isEmpty());
}


void MainWindowTest::midiTrackShowsArmButton() {
    Project project;
    project.addMidiTrack("Midi 1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    TrackPanelWidget* panel = window.m_trackRows[0].panel;
    QVERIFY(panel);
    QCOMPARE(panel->track(), &project.tracks()[0]);

    // MIDI tracks expose the record-arm button so input can be captured.
    auto* arm = panel->findChild<QPushButton*>("armButton");
    QVERIFY(arm);
    QVERIFY(arm->isVisibleTo(panel));

    arm->click();
    QVERIFY(project.tracks()[0].isRecordArmed());
    arm->click();
    QVERIFY(!project.tracks()[0].isRecordArmed());
}


void MainWindowTest::settingsDialogHasMidiInputControls() {
    Project project;
    Settings settings;
    AudioEngine engine;
    SettingsDialog dialog(settings, engine);

    auto* midiCombo = dialog.findChild<QComboBox*>("midiInputCombo");
    QVERIFY(midiCombo);
    QVERIFY(midiCombo->count() >= 1);
    QCOMPARE(midiCombo->itemData(0).toInt(), -1); // "None" default

    auto* typeCombo = dialog.findChild<QComboBox*>("midiTransportTypeCombo");
    QVERIFY(typeCombo);
    QCOMPARE(typeCombo->currentData().toInt(), settings.midiTransportControlType);

    auto* channelCombo = dialog.findChild<QComboBox*>("midiChannelCombo");
    QVERIFY(channelCombo);
    QCOMPARE(channelCombo->currentData().toInt(), settings.midiTransportChannel);

    auto* playSpin = dialog.findChild<QSpinBox*>("midiPlaySpin");
    QVERIFY(playSpin);
    QCOMPARE(playSpin->value(), settings.midiTransportPlayControl);
    auto* recordSpin = dialog.findChild<QSpinBox*>("midiRecordSpin");
    QVERIFY(recordSpin);
    QCOMPARE(recordSpin->value(), settings.midiTransportRecordControl);
    auto* stopSpin = dialog.findChild<QSpinBox*>("midiStopSpin");
    QVERIFY(stopSpin);
    QCOMPARE(stopSpin->value(), settings.midiTransportStopControl);
}


void MainWindowTest::settingsDialogLearnFlow() {
    Project project;
    Settings settings;
    AudioEngine engine;
    SettingsDialog dialog(settings, engine);

    auto* midiCombo = dialog.findChild<QComboBox*>("midiInputCombo");
    auto* playBtn = dialog.findChild<QPushButton*>("midiLearnPlayBtn");
    auto* recordBtn = dialog.findChild<QPushButton*>("midiLearnRecordBtn");
    auto* stopBtn = dialog.findChild<QPushButton*>("midiLearnStopBtn");
    QVERIFY(midiCombo);
    QVERIFY(playBtn);
    QVERIFY(recordBtn);
    QVERIFY(stopBtn);

    // With no device selected, Learn must not enter learning state.
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::None);

    if (midiCombo->count() < 2)
        QSKIP("No MIDI input device available on this machine");

    midiCombo->setCurrentIndex(1);
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::Play);

    // Clicking the same button again cancels the learning.
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::None);
}


void MainWindowTest::executeCommandAcquiresProjectWriteLock() {
    Project project;
    project.addTrack("A1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    std::atomic<bool> held{false};
    std::thread holder([&] {
        auto lk = project.readLock();
        held = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    });
    while (!held.load()) {}

    const size_t before = project.tracks().size();
    auto t0 = std::chrono::steady_clock::now();
    window.executeCommand(
        std::make_unique<AddTrackCommand>(project, static_cast<int>(before), 2));
    auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    holder.join();

    QVERIFY(dtMs >= 200); // blocked on the write lock until the reader released
    QCOMPARE(project.tracks().size(), before + 1);
}


void MainWindowTest::undoRedoAcquireProjectWriteLock() {
    Project project;
    project.addTrack("A1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    // Give undo/redo something to do: a snapshot taken before a change.
    window.m_undoStack.push(std::make_unique<SnapshotCommand>(project));
    project.tracks()[0].setVolume(0.3f);

    std::atomic<bool> held{false};
    std::thread holder([&] {
        auto lk = project.readLock();
        held = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    });
    while (!held.load()) {}

    auto t0 = std::chrono::steady_clock::now();
    window.performUndo();
    auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    holder.join();

    QVERIFY(dtMs >= 200);
    QVERIFY(project.tracks()[0].volume() != 0.3f); // snapshot restored

    // Same for redo.
    held = false;
    std::thread holder2([&] {
        auto lk = project.readLock();
        held = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    });
    while (!held.load()) {}

    t0 = std::chrono::steady_clock::now();
    window.performRedo();
    dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    holder2.join();

    QVERIFY(dtMs >= 200);
    QCOMPARE(project.tracks()[0].volume(), 0.3f);
}


void MainWindowTest::trackColorBarAssignsAndResets() {
    Project project;
    project.addTrack("A");
    project.addTrack("B");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    auto bars = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(bars.size(), 2);
    for (TrackColorBar* b : bars)
        QVERIFY(b->width() >= 4 && b->width() <= 6); // ~5px wide strip
    // Before any manual color the bars show the effective (inherited) color.
    QCOMPARE(bars[0]->color(), project.trackColor(0));
    QCOMPARE(bars[1]->color(), project.trackColor(1));

    // Assign a color to track 0 via the (stubbed) picker.
    bars[0]->setColorPickerForTesting([](const QColor&) { return QColor("#ff0044"); });
    bars[0]->pickColor();
    QVERIFY(project.tracks()[0].colorSet());
    QCOMPARE(project.tracks()[0].color(), QColor("#ff0044"));

    // The rebuild refreshed the bar to the new color; flush scheduled
    // deletions of the old rows before re-fetching.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto refreshed = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(refreshed.size(), 2);
    QCOMPARE(refreshed[0]->color(), QColor("#ff0044"));

    // Undo restores the unset (follow-the-bus) state.
    window.performUndo();
    QVERIFY(!project.tracks()[0].colorSet());

    // Reset on a track with a manual color clears it (follow the bus again).
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto bars2 = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(bars2.size(), 2);
    project.tracks()[1].setColor(QColor("#00ff00"));
    bars2[1]->resetToAutomaticColor();
    QVERIFY(!project.tracks()[1].colorSet());

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto bars3 = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(bars3[1]->color(), project.trackColor(1));
}


void MainWindowTest::trackColorTintsPanelTimelineAndEvents() {
    Project project;
    project.addMidiTrack("M1");
    // A MIDI event whose clip has no notes renders only its background, so the
    // tinted fill can be sampled directly.
    auto clip = std::make_shared<MidiClip>();
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    auto bars = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(bars.size(), 1);
    bars[0]->setColorPickerForTesting([](const QColor&) { return QColor("#ff0000"); });
    bars[0]->pickColor();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();

    QCOMPARE(project.trackColor(0), QColor("#ff0000"));
    QCOMPARE(window.m_trackRows[0].view->rowTint(), QColor("#ff0000"));

    const QColor tint = QColor("#ff0000");

    // Panel background: a light blend of the track color over the base gray
    // (text readability is kept — the row is not painted with the raw color).
    const QColor panelExpected = Project::blendColors(
        QColor("#2a2a2a"), tint, vvvdaw::TrackRowTintStrength);
    QCOMPARE(window.m_trackRows[0].panel->palette().color(QPalette::Window),
             panelExpected);

    // Timeline: the base gray with only a faint hint of the track color,
    // sampled right of the event (grid lines are vertical and can land
    // anywhere, so scan for the expected background).
    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    QCoreApplication::processEvents();
    QImage img = view->grab().toImage();
    const QColor rowExpected = Project::blendColors(
        QColor("#2a2a2a"), tint, vvvdaw::TrackTimelineTintStrength);
    bool foundRow = false;
    for (int x = 100; x < img.width() && !foundRow; ++x)
        for (int y = 5; y < img.height() - 5 && !foundRow; ++y)
            if (img.pixelColor(x, y) == rowExpected) foundRow = true;
    QVERIFY(foundRow);

    // Event background: exactly the picked color. The event spans samples
    // 0..48000 (x 0..48 at the default zoom); x=20 avoids the grid lines and
    // the left border.
    QCOMPARE(img.pixelColor(20, img.height() / 2), tint);

    // The bottom resize handle takes the colors of the row above it: the
    // panel's blended color on the panel column, the timeline color on the
    // timeline column — no dark default stripe remains.
    QWidget* handle = window.m_trackRows[0].row->findChild<QWidget*>("trackResizeHandle");
    QVERIFY(handle);
    QImage rowImg = window.m_trackRows[0].row->grab().toImage();
    const int hy = rowImg.height() - vvvdaw::TrackResizeHandleHeight + 2;
    const QColor handlePanelExpected = Project::blendColors(
        QColor("#2a2a2a"), tint, vvvdaw::TrackRowTintStrength);
    const QColor handleExpected = Project::blendColors(
        QColor("#2a2a2a"), tint, vvvdaw::TrackTimelineTintStrength);
    QCOMPARE(rowImg.pixelColor(100, hy), handlePanelExpected);   // panel column
    QCOMPARE(rowImg.pixelColor(rowImg.width() - 50, hy), handleExpected); // timeline
}


void MainWindowTest::trackColorPaletteUsesTrackScheme() {
    // The suggested swatches are bright/saturated, matching the automatic
    // per-track tints (not the muted bus strip values).
    const QList<QColor> palette = BusColorPaletteDialog::suggestedColors(
        TrackColorBar::paletteScheme());
    QVERIFY(palette.size() >= 9);
    for (const QColor& c : palette) {
        QVERIFY(c.isValid());
        QCOMPARE(c.hsvSaturation(), vvvdaw::AutoTrackSaturation);
        QCOMPARE(c.value(), vvvdaw::AutoTrackValue);
    }
    QCOMPARE(palette.first(), QColor::fromHsv(0, vvvdaw::AutoTrackSaturation,
                                              vvvdaw::AutoTrackValue));

    // Opening on a gray color defaults S/V to the bright track values, not
    // to the gray's (and not to the bus scheme's muted ones).
    BusColorPaletteDialog dialog(QColor("#2e2e2e"), nullptr,
                                 TrackColorBar::paletteScheme());
    QCOMPARE(dialog.selectedColor().hsvSaturation(), vvvdaw::AutoTrackSaturation);
    QCOMPARE(dialog.selectedColor().value(), vvvdaw::AutoTrackValue);

    // The bus scheme defaults stay muted.
    BusColorPaletteDialog busDialog(QColor("#2e2e2e"), nullptr);
    QCOMPARE(busDialog.selectedColor().hsvSaturation(), vvvdaw::AutoStripSaturation);
    QCOMPARE(busDialog.selectedColor().value(), vvvdaw::AutoStripValue);
}


void MainWindowTest::trackRowTintKeepsPanelSubtle() {
    Project project;
    project.addTrack("A");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    // A bright yellow row: the panel only takes a light blend of the color
    // (not the raw color), so the fixed light text stays readable.
    auto bars = window.findChildren<TrackColorBar*>("trackColorBar");
    QCOMPARE(bars.size(), 1);
    bars[0]->setColorPickerForTesting([](const QColor&) { return QColor("#ffff00"); });
    bars[0]->pickColor();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(project.trackColor(0), QColor("#ffff00"));

    TrackPanelWidget* panel = window.m_trackRows[0].panel;
    const QColor expected = Project::blendColors(
        QColor("#2a2a2a"), QColor("#ffff00"), vvvdaw::TrackRowTintStrength);
    QCOMPARE(panel->palette().color(QPalette::Window), expected);
    QVERIFY(expected != QColor("#ffff00")); // subtle, not the raw color

    // The text color is unchanged: the light theme color for name and labels.
    QLineEdit* nameEdit = panel->findChild<QLineEdit*>();
    QVERIFY(nameEdit);
    QVERIFY(nameEdit->styleSheet().contains("#ccc"));
    QLabel* panLabel = nullptr;
    for (QLabel* lbl : panel->findChildren<QLabel*>())
        if (lbl->text() == "pan:") panLabel = lbl;
    QVERIFY(panLabel);
    QVERIFY(panLabel->styleSheet().contains("#aaa"));

    // The exact color is reserved for the event backgrounds.
    TrackViewWidget* view = window.m_trackRows[0].view;
    QCOMPARE(view->rowTint(), QColor("#ffff00"));
}


void MainWindowTest::newTrackInheritsPanelAndHeight() {
    Project project;
    project.addTrack("A");
    project.tracks()[0].setPluginPanelWidth(320);
    project.tracks()[0].setHeight(260);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    // Adding a track (as the context menu does) rebuilds the rows: the new
    // track inherits the effects-panel width and the bottom row's height.
    window.executeCommand(std::make_unique<AddTrackCommand>(
        project, static_cast<int>(project.tracks().size()), 2));
    QCOMPARE(project.tracks().size(), size_t(2));
    QCOMPARE(project.tracks()[1].pluginPanelWidth(), 320);
    QCOMPARE(project.tracks()[1].height(), 260);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_trackRows.size(), size_t(2));
    // The exact pixel size depends on the window width; both rows share it.
    QCOMPARE(window.m_trackRows[1].innerSplitter->sizes().value(0),
             window.m_trackRows[0].innerSplitter->sizes().value(0));
    QVERIFY(window.m_trackRows[1].innerSplitter->sizes().value(0) > 0);
    QCOMPARE(window.m_trackRows[1].row->rowHeight(), 260);

    // A collapsed (hidden) panel stays hidden on the new row.
    project.tracks().back().setPluginPanelWidth(0);
    window.executeCommand(std::make_unique<AddTrackCommand>(
        project, static_cast<int>(project.tracks().size()), 2));
    QCOMPARE(project.tracks()[2].pluginPanelWidth(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_trackRows.size(), size_t(3));
    QCOMPARE(window.m_trackRows[2].innerSplitter->sizes().value(0), 0);
}


QTEST_MAIN(MainWindowTest)
#include "test_gui_mainwindow.moc"
