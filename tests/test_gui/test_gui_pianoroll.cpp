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
#include "gui/VelocityEditorWidget.h"
#include "gui/ControlEventEditorWidget.h"
#include "gui/ChannelRoutingDialog.h"
#include "gui/SettingsDialog.h"
#include "commands/TrackCommands.h"
#include "commands/SnapshotCommand.h"
#include "GuiTestHelpers.h"

class PianoRollTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void previewTargetFollowsFocusedPianoRoll();
    void pianoRollMiddleDragPans();
    void pianoRollCtrlWheelZoomAnchorsCursor();
    void channelComboUpdatesTrackChannel();
    void controlLaneEditorDrawsAndDeletesEvents();
    void velocityBarsAreNarrowAndAnchoredAtNoteStart();
    void selectedVelocityBarRendersOnTop();
private:
    GuiTestEnv m_env;
};

void PianoRollTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void PianoRollTest::cleanupTestCase() {
    m_env.cleanup();
}

void PianoRollTest::previewTargetFollowsFocusedPianoRoll() {
    Project project;
    project.addMidiTrack("Midi 1");
    project.addMidiTrack("Midi 2");

    auto addEvent = [&project](int trackIndex) -> int64_t {
        auto clip = std::make_shared<MidiClip>();
        clip->setLengthTicks(MidiClip::kPPQ);
        MidiEvent ev;
        ev.setClip(clip);
        ev.setStartSample(0);
        ev.setDurationSample(48000);
        project.tracks()[static_cast<size_t>(trackIndex)].addMidiEvent(ev);
        return project.tracks()[static_cast<size_t>(trackIndex)].midiEvents().back().id();
    };
    const int64_t id0 = addEvent(0);
    const int64_t id1 = addEvent(1);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    // No piano roll open: nothing to preview into.
    QCOMPARE(engine.midiPreviewTrack(), -1);

    // Opening the first piano roll routes preview into its track.
    window.openPianoRoll(0, id0);
    QCOMPARE(window.m_pianoRollWindows.size(), size_t(1));
    QCOMPARE(engine.midiPreviewTrack(), 0);

    // Opening the second makes it the active preview target.
    window.openPianoRoll(1, id1);
    QCOMPARE(window.m_pianoRollWindows.size(), size_t(2));
    QCOMPARE(engine.midiPreviewTrack(), 1);

    // Switching focus back (what the WindowActivate filter calls) retargets.
    window.setActiveMidiPreview(0, id0);
    QCOMPARE(engine.midiPreviewTrack(), 0);
}


void PianoRollTest::pianoRollMiddleDragPans() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 16); // content wider than the viewport
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* widget = window.m_pianoRollWindows[0]->findChild<PianoRollWidget*>();
    QVERIFY(widget);
    auto* scrollArea = widget->enclosingScrollArea();
    QVERIFY(scrollArea);
    QScrollBar* hbar = scrollArea->horizontalScrollBar();
    QVERIFY(hbar->maximum() > 0); // there is room to scroll

    hbar->setValue(50);
    QCOMPARE(hbar->value(), 50);

    // Drag right with the middle button: content follows, scroll decreases.
    QTest::mousePress(widget, Qt::MiddleButton, Qt::NoModifier, QPoint(300, 100));
    QTest::mouseMove(widget, QPoint(340, 100));
    QCOMPARE(hbar->value(), 10);
    QTest::mouseMove(widget, QPoint(300, 100));
    QCOMPARE(hbar->value(), 50);
    QTest::mouseMove(widget, QPoint(250, 100));
    QCOMPARE(hbar->value(), 100);

    // After release, moves no longer pan.
    QTest::mouseRelease(widget, Qt::MiddleButton, Qt::NoModifier, QPoint(250, 100));
    QTest::mouseMove(widget, QPoint(350, 100));
    QCOMPARE(hbar->value(), 100);
}


void PianoRollTest::pianoRollCtrlWheelZoomAnchorsCursor() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 16);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* widget = window.m_pianoRollWindows[0]->findChild<PianoRollWidget*>();
    QVERIFY(widget);
    auto* scrollArea = widget->enclosingScrollArea();
    QVERIFY(scrollArea);
    QScrollBar* hbar = scrollArea->horizontalScrollBar();
    QVERIFY(hbar->maximum() > 0);

    const double oldPps = 0.06; // PianoRollWidget default
    hbar->setValue(100);
    const int viewportX = 300; // cursor position within the viewport
    const int mouseX = viewportX + 100;

    QWheelEvent we(QPointF(mouseX, 100), QPointF(mouseX, 100), QPoint(0, 0), QPoint(0, 120),
                   Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &we);
    QCoreApplication::processEvents();

    const double newPps = std::clamp(oldPps * 1.2, 0.004, 2.0);
    QVERIFY(newPps > oldPps);
    const double anchorTick = static_cast<double>(mouseX - 56) / oldPps;
    const int expected = std::max(0, static_cast<int>(std::lround(
        anchorTick * newPps - viewportX + 56)));
    QCOMPARE(hbar->value(), expected);
}


void PianoRollTest::channelComboUpdatesTrackChannel() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* combo = window.m_pianoRollWindows[0]->findChild<QComboBox*>("midiChannelCombo");
    QVERIFY(combo);
    QCOMPARE(combo->currentIndex(), 0); // default channel 1

    combo->setCurrentIndex(5); // channel 6
    QCOMPARE(project.tracks()[0].midiChannel(), 5);

    // Undo reverts the channel; reload keeps the combo aligned with the model.
    window.m_undoStack.undo();
    window.resyncPianoRollWindows();
    QCOMPARE(project.tracks()[0].midiChannel(), 0);
    QCOMPARE(combo->currentIndex(), 0);
}


void PianoRollTest::controlLaneEditorDrawsAndDeletesEvents() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 16);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* lane = window.m_pianoRollWindows[0]->findChild<ControlEventEditorWidget*>();
    QVERIFY(lane);
    QVERIFY(lane->width() > 200);

    // A click in the lane draws a CC1 (default lane) control event. With the
    // default zoom (0.06 px/tick) and snap (1/4 beat = 240 ticks) x=200 maps
    // to tick 2400, y=20 to a value of 95.
    QTest::mouseClick(lane, Qt::LeftButton, Qt::NoModifier, QPoint(200, 20));
    QCOMPARE(clip->controlEvents().size(), size_t(1));
    QCOMPARE(clip->controlEvents()[0].number, uint8_t(1));
    QCOMPARE(clip->controlEvents()[0].startTick, int64_t(2400));
    QCOMPARE(clip->controlEvents()[0].value, 95);

    // The draw gesture is one undoable step.
    window.m_undoStack.undo();
    QCOMPARE(clip->controlEvents().size(), size_t(0));

    // Redo restores it, then right-clicking the point removes it again.
    window.m_undoStack.redo();
    QCOMPARE(clip->controlEvents().size(), size_t(1));
    QTest::mouseClick(lane, Qt::RightButton, Qt::NoModifier, QPoint(200, 20));
    QCOMPARE(clip->controlEvents().size(), size_t(0));
}


void PianoRollTest::velocityBarsAreNarrowAndAnchoredAtNoteStart() {
    // The velocity bar must be a thin column starting at the note's start tick,
    // not spanning the whole note's duration.
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 4);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);
    const int64_t eventId = project.tracks()[0].midiEvents().back().id();

    clip->addNote(60, 127, 0, MidiClip::kPPQ); // long note starting at tick 0

    UndoStack undo;
    VelocityEditorWidget vel(project, undo, 0, eventId);
    vel.resize(256, 80);
    vel.reload();

    QImage img(vel.size(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    vel.render(&img);

    // Bar occupied at the note start (inside the thin 5px column).
    QVERIFY(img.pixelColor(56 + 2, 40).blue() > 150); // note bar blue #3d7bbf
    // A few pixels further the bar is gone: it does not span the full duration.
    QCOMPARE(img.pixelColor(56 + 15, 40), QColor("#232323"));
}


void PianoRollTest::selectedVelocityBarRendersOnTop() {
    // Two notes landing on the same time unit overlap fully; selecting the
    // earlier-drawn one must bring its bar to the front (drawn last/on top).
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 4);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);
    const int64_t eventId = project.tracks()[0].midiEvents().back().id();

    int64_t a = clip->addNote(60, 127, 0, MidiClip::kPPQ);
    clip->addNote(72, 127, 0, MidiClip::kPPQ); // added later, drawn first by default

    UndoStack undo;
    VelocityEditorWidget vel(project, undo, 0, eventId);
    vel.resize(256, 80);
    vel.reload();

    auto render = [&vel]() {
        QImage img(vel.size(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        vel.render(&img);
        return img;
    };

    // No selection: the later note's bar (blue) naturally sits on top.
    QCOMPARE(render().pixelColor(56 + 2, 40), QColor("#3d7bbf"));

    // Selecting the earlier note promotes its bar to the front (cyan).
    vel.setSelection({a});
    QCOMPARE(render().pixelColor(56 + 2, 40), QColor("#66ccff"));
}


QTEST_MAIN(PianoRollTest)
#include "test_gui_pianoroll.moc"
