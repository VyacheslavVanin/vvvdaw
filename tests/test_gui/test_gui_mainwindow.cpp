#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QListWidget>
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
#include "gui/PluginWindow.h"
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
    void mainWindowFileMenuHasSaveAsTemplate();
    void replaceProjectSwapsAndRebuilds();
    void midiTrackShowsArmButton();
    void settingsDialogHasMidiInputControls();
    void settingsDialogLearnFlow();
    void executeCommandAcquiresProjectWriteLock();
    void undoRedoAcquireProjectWriteLock();
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


QTEST_MAIN(MainWindowTest)
#include "test_gui_mainwindow.moc"
