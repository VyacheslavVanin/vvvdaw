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

class PianoRollTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void previewTargetFollowsFocusedPianoRoll();
    void pianoRollMiddleDragPans();
    void pianoRollCtrlWheelZoomAnchorsCursor();
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


QTEST_MAIN(PianoRollTest)
#include "test_gui_pianoroll.moc"
