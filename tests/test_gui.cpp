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
    void moveAudioEventBetweenAudioTracks();
    void moveMidiEventBetweenMidiTracks();
    void midiCrossTrackMoveKeepsSiblingEvents();
    void shiftDragCreatesIndependentMidiCopy();
    void shiftDragOnAudioDoesNotDuplicate();
    void rejectAudioEventToMidiTrack();
    void rejectMidiEventToAudioTrack();
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

void MainWindowTest::moveAudioEventBetweenAudioTracks() {
    Project project;
    project.addTrack("A1");
    project.addTrack("A2");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    AudioEvent ev;
    ev.setStartSample(100);
    src.addEvent(ev);
    const int64_t id = src.events().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.moveEventToTrack(0, 1, id, 500));
    QVERIFY(src.events().empty());
    QCOMPARE(dst.events().size(), size_t(1));
    QCOMPARE(dst.events().front().id(), id);
    QCOMPARE(dst.events().front().startSample(), int64_t(500));
}

void MainWindowTest::moveMidiEventBetweenMidiTracks() {
    Project project;
    project.addMidiTrack("M1");
    project.addMidiTrack("M2");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    MidiEvent ev;
    ev.setStartSample(100);
    src.addMidiEvent(ev);
    const int64_t id = src.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.moveEventToTrack(0, 1, id, 500));
    QVERIFY(src.midiEvents().empty());
    QCOMPARE(dst.midiEvents().size(), size_t(1));
    QCOMPARE(dst.midiEvents().front().id(), id);
    QCOMPARE(dst.midiEvents().front().startSample(), int64_t(500));
}

void MainWindowTest::midiCrossTrackMoveKeepsSiblingEvents() {
    Project project;
    project.addMidiTrack("M1");
    project.addMidiTrack("M2");
    Track& a = project.tracks()[0];
    Track& b = project.tracks()[1];

    MidiEvent ea;
    ea.setStartSample(0);
    a.addMidiEvent(ea);
    MidiEvent eb;
    eb.setStartSample(100);
    a.addMidiEvent(eb);
    MidiEvent ec;
    ec.setStartSample(0);
    b.addMidiEvent(ec);
    QCOMPARE(a.midiEvents().size(), size_t(2));
    QCOMPARE(b.midiEvents().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    // A's first event (id 1) collides with B's own first event (id 1) when moved in.
    const int64_t aFirstId = a.midiEvents()[0].id();
    QVERIFY(window.moveEventToTrack(0, 1, aFirstId, 500));
    QCOMPARE(b.midiEvents().size(), size_t(2));
    QVERIFY(b.midiEvents()[0].id() != b.midiEvents()[1].id());

    // Moving one of B's two events out must not take the sibling along.
    const int64_t moveOutId = b.midiEvents()[0].id();
    QVERIFY(window.moveEventToTrack(1, 0, moveOutId, 600));
    QCOMPARE(b.midiEvents().size(), size_t(1)); // sibling survives
    QCOMPARE(a.midiEvents().size(), size_t(2));
}

void MainWindowTest::shiftDragCreatesIndependentMidiCopy() {
    Project project;
    project.addMidiTrack("M1");
    Track& track = project.tracks()[0];
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);
    QCOMPARE(track.midiEvents().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002); // event spans 96 px, click well inside it
    QVERIFY(view->isVisible());

    QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
    QCOMPARE(track.midiEvents().size(), size_t(2));
    QVERIFY(track.midiEvents()[0].clip() != track.midiEvents()[1].clip());
    QCOMPARE(track.midiEvents()[1].clip()->notes().size(), size_t(1));

    // Editing the copy's clip must not affect the original event's clip.
    track.midiEvents()[1].clip()->addNote(72, 120, 240, 240);
    QCOMPARE(track.midiEvents()[1].clip()->notes().size(), size_t(2));
    QCOMPARE(track.midiEvents()[0].clip()->notes().size(), size_t(1));

    QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
}

void MainWindowTest::shiftDragOnAudioDoesNotDuplicate() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);
    QCOMPARE(track.events().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);
    QVERIFY(view->isVisible());

    // Shift on an audio event is a plain move, not a duplicate.
    QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
    QCOMPARE(track.events().size(), size_t(1));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
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

QTEST_MAIN(MainWindowTest)
#include "test_gui.moc"
