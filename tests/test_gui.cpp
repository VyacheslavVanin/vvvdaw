#include <QTest>
#include <QApplication>
#include <algorithm>
#include <portaudio.h>

#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "gui/MainWindow.h"
#include "gui/TrackPanelWidget.h"
#include "gui/TrackViewWidget.h"
#include "gui/TimelineRuler.h"
#include "gui/MeasureRuler.h"
#include "gui/BusPanelWidget.h"
#include "gui/InstrumentPanelWidget.h"

// Integration tests for MainWindow::setupUi / rebuildTracks. They run on the
// offscreen Qt platform and a real PortAudio initialization so that device
// enumeration inside rebuildTracks works; they assert the widget structure
// and that rebuildTracks() tracks the project's contents.
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
};

void MainWindowTest::initTestCase() {
    if (Pa_Initialize() != paNoError)
        QSKIP("PortAudio not available");
}

void MainWindowTest::cleanupTestCase() {
    Pa_Terminate();
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

QTEST_MAIN(MainWindowTest)
#include "test_gui.moc"
