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

class TrackViewTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void moveAudioEventBetweenAudioTracks();
    void moveMidiEventBetweenMidiTracks();
    void midiCrossTrackMoveKeepsSiblingEvents();
    void shiftDragCreatesIndependentMidiCopy();
    void shiftDragOnAudioDoesNotDuplicate();
    void multiSelectAudioEventsCtrlAndShift();
    void deleteAllSelectedEvents();
    void crossfadeContextMenuAppliesAndUndoes();
    void middleDragPansTrackView();
    void ctrlWheelZoomAnchorsCursorFrame();
    void audioEventBorderStaysAtTrueEdgeDuringDeepZoom();
    void midiEventBorderStaysAtTrueEdgeDuringDeepZoom();
    void trackViewMouseCursorTracksAndClears();
    void trackViewContextMenuCutSplitsEvent();
    void trackViewContextMenuCutAndSnapAlignsToGrid();
    void sameTrackDragUndoRestoresPosition();
    void edgeTrimUndoRestoresEdges();
    void midiSameTrackDragUndoRestoresPosition();
    void midiEdgeTrimUndoRestoresEdges();
private:
    GuiTestEnv m_env;
};

void TrackViewTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void TrackViewTest::cleanupTestCase() {
    m_env.cleanup();
}

void TrackViewTest::moveAudioEventBetweenAudioTracks() {
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


void TrackViewTest::moveMidiEventBetweenMidiTracks() {
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


void TrackViewTest::midiCrossTrackMoveKeepsSiblingEvents() {
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


void TrackViewTest::shiftDragCreatesIndependentMidiCopy() {
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


void TrackViewTest::shiftDragOnAudioDoesNotDuplicate() {
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


void TrackViewTest::multiSelectAudioEventsCtrlAndShift() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    for (int i = 0; i < 3; ++i) {
        AudioEvent ev;
        ev.setStartSample(i * 48000);
        ev.setDurationSample(48000);
        track.addEvent(ev);
    }

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001); // 1 px = 1000 samples; each event spans 48 px

    // Plain click selects a single event (anchor becomes event 0).
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(1));

    // Ctrl+click adds the third event to the selection.
    QTest::mousePress(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(2));

    // Ctrl+click on an already-selected event removes it again.
    QTest::mousePress(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(1));

    // Shift+click range-selects from the anchor (event 2 after the toggles) to
    // the clicked event (event 1): events 1 and 2 become selected.
    QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(72, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(2));

    // Re-anchor on event 0, then Shift+click event 2 selects all three.
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(120, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(3));

    // Clicking empty space clears the selection.
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(350, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(0));
}


void TrackViewTest::deleteAllSelectedEvents() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    for (int i = 0; i < 3; ++i) {
        AudioEvent ev;
        ev.setStartSample(i * 48000);
        ev.setDurationSample(48000);
        track.addEvent(ev);
    }

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);

    // Select events 0 and 2 (plain click + Ctrl+click).
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mousePress(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ControlModifier, QPoint(120, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(2));

    view->deleteSelectedEvent();
    QCOMPARE(track.events().size(), size_t(1));
    QCOMPARE(track.events()[0].startSample(), int64_t(48000)); // middle one remains
    QVERIFY(!view->hasSelection());
}


void TrackViewTest::crossfadeContextMenuAppliesAndUndoes() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    for (int i = 0; i < 2; ++i) {
        AudioEvent ev;
        ev.setStartSample(i * 48000);
        ev.setDurationSample(48000);
        track.addEvent(ev);
    }

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001); // 1 px = 1000 samples
    view->setScrollOffset(0);

    // Select both events: plain click on event 0, Ctrl+click on event 1.
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(24, 40));
    QTest::mousePress(view, Qt::LeftButton, Qt::ControlModifier, QPoint(72, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ControlModifier, QPoint(72, 40));
    QCOMPARE(view->selectedEventIds().size(), size_t(2));

    // Default crossfade length: 5 ms at 48 kHz.
    const int64_t defaultFade =
        static_cast<int64_t>(project.sampleRate() * vvvdaw::DefaultCrossfadeMs / 1000);
    QVERIFY(defaultFade > 0);

    bool found = false;
    QTimer::singleShot(0, [&] {
        if (auto* menu = view->findChild<QMenu*>()) {
            for (QAction* a : menu->actions()) {
                if (a->text() == "Crossfade Selected Events") {
                    a->trigger();
                    found = true;
                    break;
                }
            }
            menu->close();
        }
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, QPoint(24, 40),
                          view->mapToGlobal(QPoint(24, 40)));
    QApplication::sendEvent(view, &ctx);
    QCoreApplication::processEvents();

    QVERIFY(found);
    const AudioEvent& ev0 = project.tracks()[0].events()[0];
    const AudioEvent& ev1 = project.tracks()[0].events()[1];
    QCOMPARE(ev0.fadeOutSamples(), defaultFade);
    QCOMPARE(ev1.fadeInSamples(), defaultFade);
    QCOMPARE(ev0.fadeInSamples(), int64_t(0));
    QCOMPARE(ev1.fadeOutSamples(), int64_t(0));

    window.performUndo();
    QCOMPARE(project.tracks()[0].events()[0].fadeOutSamples(), int64_t(0));
    QCOMPARE(project.tracks()[0].events()[1].fadeInSamples(), int64_t(0));

    window.performRedo();
    QCOMPARE(project.tracks()[0].events()[0].fadeOutSamples(), defaultFade);
    QCOMPARE(project.tracks()[0].events()[1].fadeInSamples(), defaultFade);
}


void TrackViewTest::middleDragPansTrackView() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);            // 1 px = 500 samples
    view->setScrollOffset(48000);

    // Drag right with the middle button: the content follows the cursor, so
    // the scroll offset decreases.
    QTest::mousePress(view, Qt::MiddleButton, Qt::NoModifier, QPoint(100, 40));
    QTest::mouseMove(view, QPoint(150, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 - 50 * 500));

    // Drag left: the offset tracks the total drag delta from the press.
    QTest::mouseMove(view, QPoint(50, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 50 * 500));
    QTest::mouseMove(view, QPoint(0, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 100 * 500));

    // Releasing the middle button stops the pan: moves no longer scroll.
    QTest::mouseRelease(view, Qt::MiddleButton, Qt::NoModifier, QPoint(0, 40));
    QTest::mouseMove(view, QPoint(300, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 100 * 500));
}


void TrackViewTest::ctrlWheelZoomAnchorsCursorFrame() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);
    view->setScrollOffset(0);

    const QPoint pos(200, 40);
    const int64_t before = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());

    auto sendZoomWheel = [view](const QPoint& p, int deltaY) {
        QWheelEvent ev(QPointF(p), QPointF(p), QPoint(0, 0), QPoint(0, deltaY),
                       Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view, &ev);
    };

    // First zoom-in notch: the frame under the cursor must not move.
    sendZoomWheel(pos, 120);
    QVERIFY(view->zoom() > 0.001);
    QVERIFY(view->scrollOffset() != 0);
    const int64_t after1 = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());
    QCOMPARE(after1, before);

    // A second notch keeps the same frame anchored.
    sendZoomWheel(pos, 120);
    const int64_t after2 = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());
    QCOMPARE(after2, before);

    // And zooming back out restores the original view.
    sendZoomWheel(pos, -120);
    sendZoomWheel(pos, -120);
    QCOMPARE(view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom()), before);
}


void TrackViewTest::audioEventBorderStaysAtTrueEdgeDuringDeepZoom() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];

    std::vector<float> samples;
    for (int i = 0; i < 4096; ++i)
        samples.push_back((i % 2 == 0) ? 0.7f : -0.7f);
    auto clip = std::make_shared<AudioClip>(std::move(samples), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(clip->frameCount());
    ev.setSourceFrames(clip->frameCount());
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::SampleViewPixelsPerSample); // 4 px per sample

    // Scroll so the event's true right edge (sample 4096) lands at x=360 while
    // its left edge is far off-screen: the event is much wider than the
    // viewport, so a viewport-clamped rect used to push the border off-screen
    // at deep zoom. The border must instead be drawn at the true right edge.
    const int64_t scroll = 4096 - 360 / 4; // event right edge -> x=360
    view->setScrollOffset(scroll);
    QCoreApplication::processEvents();

    QImage img = view->grab().toImage();

    const int rightX = static_cast<int>((4096 - scroll) * 4); // 360
    // The waveform of the visible tail is present up to the true right edge.
    QVERIFY(regionHasWaveform(img, 0, rightX - 1, 3, 78));
    // The border is drawn at the true right edge (top border row y=2).
    QVERIFY(regionHasWaveform(img, rightX - 1, rightX + 1, 2, 2));
    // Nothing of the event extends past its true right edge.
    QVERIFY(regionIsBackground(img, rightX + 5, 399, 2, 78));
}

void TrackViewTest::midiEventBorderStaysAtTrueEdgeDuringDeepZoom() {
    Project project;
    project.addMidiTrack("M1");
    Track& track = project.tracks()[0];

    auto clip = std::make_shared<MidiClip>();
    // A note spanning the whole event so its fill is visible at deep zoom.
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::SampleViewPixelsPerSample);

    // Same geometry as the audio case: the event is much wider than the
    // viewport, and its true right edge lands at x=360.
    const int64_t scroll = 48000 - 360 / 4;
    view->setScrollOffset(scroll);
    QCoreApplication::processEvents();

    QImage img = view->grab().toImage();

    const int rightX = static_cast<int>((48000 - scroll) * 4); // 360
    // The event border is drawn at the true right edge (top border row y=2).
    QVERIFY(regionHasWaveform(img, rightX - 1, rightX + 1, 2, 2));
    // Nothing of the event extends past its true right edge.
    QVERIFY(regionIsBackground(img, rightX + 5, 399, 2, 78));
}

void TrackViewTest::trackViewMouseCursorTracksAndClears() {
    Project project;
    project.addTrack("A1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);

    QCOMPARE(view->mouseCursorX(), -1);

    QTest::mouseMove(view, QPoint(150, 40));
    QCOMPARE(view->mouseCursorX(), 150);

    QTest::mouseMove(view, QPoint(37, 40));
    QCOMPARE(view->mouseCursorX(), 37);

    // Leaving the widget clears the thin cursor line.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view, &leave);
    QCOMPARE(view->mouseCursorX(), -1);
}


void TrackViewTest::trackViewContextMenuCutSplitsEvent() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);       // 1 px = 1000 samples
    view->setScrollOffset(0);

    // Event spans samples [0, 48000) => pixels [0, 48). Right-click in the
    // middle at pixel 24 => sample 24000 (no grid snap).
    const QPoint menuPos(24, 40);
    const int64_t cutSample = 24000;

    bool cutFound = false;
    QTimer::singleShot(0, [&] {
        if (auto* menu = view->findChild<QMenu*>()) {
            for (QAction* a : menu->actions()) {
                if (a->text() == "Cut") {
                    a->trigger();
                    cutFound = true;
                    break;
                }
            }
            // A programmatic trigger() does not dismiss the popup; close it so
            // menu.exec() returns and the pending cut is emitted.
            menu->close();
        }
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, menuPos,
                          view->mapToGlobal(menuPos));
    QApplication::sendEvent(view, &ctx);
    QCoreApplication::processEvents();

    QVERIFY(cutFound);
    QCOMPARE(project.tracks()[0].events().size(), size_t(2));
    QCOMPARE(project.tracks()[0].events()[0].endSample(),
             project.tracks()[0].events()[1].startSample());
    QCOMPARE(project.tracks()[0].events()[1].startSample(), cutSample);
}


void TrackViewTest::trackViewContextMenuCutAndSnapAlignsToGrid() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);       // 1 px = 1000 samples
    view->setScrollOffset(0);

    // Snap unit = samplesPerBar / resolution = 96000 / 4 = 24000. The cut at
    // sample 26000 (pixel 26) lands after the grid line, so the split is pulled
    // left to 24000 and the right piece starts flush on the grid line.
    const QPoint menuPos(26, 40);

    bool snapCutFound = false;
    QTimer::singleShot(0, [&] {
        if (auto* menu = view->findChild<QMenu*>()) {
            for (QAction* a : menu->actions()) {
                if (a->text() == "Cut and Snap") {
                    a->trigger();
                    snapCutFound = true;
                    break;
                }
            }
            menu->close();
        }
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, menuPos,
                          view->mapToGlobal(menuPos));
    QApplication::sendEvent(view, &ctx);
    QCoreApplication::processEvents();

    QVERIFY(snapCutFound);
    QCOMPARE(project.tracks()[0].events().size(), size_t(2));
    const AudioEvent& left = project.tracks()[0].events()[0];
    const AudioEvent& right = project.tracks()[0].events()[1];
    QCOMPARE(left.endSample(), int64_t(24000));
    QCOMPARE(right.startSample(), int64_t(24000)); // snapped to the grid line
    QCOMPARE(left.endSample(), right.startSample());
}

// Regression: undo/redo/execute of a command must acquire the project write
// lock, otherwise the audio callback thread (which reads the project under a
// shared lock) can be destroyed underneath by wholesale mutations (e.g. undo's
// Project::fromJson clearing plugin chains) -> use-after-free in
// PluginChain::process. Each case holds the project's shared lock from another
// thread for 300 ms and asserts the GUI call blocks until it is released.

void TrackViewTest::sameTrackDragUndoRestoresPosition() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);       // 1 px = 500 samples
    view->setScrollOffset(0);
    view->setSnapToGrid(false);

    // Drag the event 40 px right: start moves 40 * 500 = 20000 samples.
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseMove(view, QPoint(80, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(80, 40));
    QCOMPARE(track.events()[0].startSample(), int64_t(20000));

    window.performUndo();
    QCOMPARE(track.events()[0].startSample(), int64_t(0));
    QCOMPARE(track.events()[0].durationSample(), int64_t(48000));
}


void TrackViewTest::edgeTrimUndoRestoresEdges() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);       // 1 px = 500 samples; event spans 96 px
    view->setScrollOffset(0);
    view->setSnapToGrid(false);

    // Right edge: drag out by 8 px -> duration grows by 4000 samples.
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(92, 40));
    QTest::mouseMove(view, QPoint(100, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(100, 40));
    QCOMPARE(track.events()[0].durationSample(), int64_t(52000));
    QCOMPARE(track.events()[0].startSample(), int64_t(0));

    window.performUndo();
    QCOMPARE(track.events()[0].durationSample(), int64_t(48000));
    QCOMPARE(track.events()[0].startSample(), int64_t(0));

    // Left edge: drag right by 7 px -> start/offset advance, duration shrinks.
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(3, 40));
    QTest::mouseMove(view, QPoint(10, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(10, 40));
    QCOMPARE(track.events()[0].startSample(), int64_t(3500));
    QCOMPARE(track.events()[0].offsetSample(), int64_t(3500));
    QCOMPARE(track.events()[0].durationSample(), int64_t(44500));

    window.performUndo();
    QCOMPARE(track.events()[0].startSample(), int64_t(0));
    QCOMPARE(track.events()[0].offsetSample(), int64_t(0));
    QCOMPARE(track.events()[0].durationSample(), int64_t(48000));
}


void TrackViewTest::midiSameTrackDragUndoRestoresPosition() {
    Project project;
    project.addMidiTrack("M1");
    Track& track = project.tracks()[0];
    MidiEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);       // 1 px = 500 samples
    view->setScrollOffset(0);
    view->setSnapToGrid(false);

    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseMove(view, QPoint(80, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(80, 40));
    QCOMPARE(track.midiEvents()[0].startSample(), int64_t(20000));

    window.performUndo();
    QCOMPARE(track.midiEvents()[0].startSample(), int64_t(0));
    window.performRedo();
    QCOMPARE(track.midiEvents()[0].startSample(), int64_t(20000));
}


void TrackViewTest::midiEdgeTrimUndoRestoresEdges() {
    Project project;
    project.addMidiTrack("M1");
    Track& track = project.tracks()[0];
    MidiEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);       // 1 px = 500 samples; event spans 96 px
    view->setScrollOffset(0);
    view->setSnapToGrid(false);

    // Right edge: drag out by 8 px -> duration grows by 4000 samples.
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(92, 40));
    QTest::mouseMove(view, QPoint(100, 40));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(100, 40));
    QCOMPARE(track.midiEvents()[0].durationSample(), int64_t(52000));

    window.performUndo();
    QCOMPARE(track.midiEvents()[0].durationSample(), int64_t(48000));
    QCOMPARE(track.midiEvents()[0].startSample(), int64_t(0));
    QCOMPARE(track.midiEvents()[0].offsetSample(), int64_t(0));
}


QTEST_MAIN(TrackViewTest)
#include "test_gui_trackview.moc"
