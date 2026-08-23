#include <QTest>
#include <memory>
#include "core/UndoStack.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/MidiEvent.h"
#include "model/MidiClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "commands/TrackCommands.h"
#include "commands/BusCommands.h"
#include "commands/ProjectCommands.h"
#include "commands/EventCommands.h"
#include "commands/MidiCommands.h"
#include "commands/InstrumentCommands.h"
#include "commands/SnapshotCommand.h"

class TestProjectCommands : public QObject {
    Q_OBJECT
private slots:
    void setTempoCommand();
    void setTempoCommandMerges();
    void setTimeSigCommand();
};

void TestProjectCommands::setTempoCommand() {
    Project p;
    p.setSampleRate(48000);
    p.setTempo(120.0);
    Track* t = p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(100);
    ev.setDurationSample(100);
    t->addEvent(ev);

    UndoStack stack;
    stack.execute(std::make_unique<SetTempoCommand>(p, 120.0, 240.0));
    QCOMPARE(p.tempo(), 240.0);
    QCOMPARE(t->events()[0].startSample(), int64_t(50)); // rescaled by 120/240

    stack.undo();
    QCOMPARE(p.tempo(), 120.0);
    QCOMPARE(t->events()[0].startSample(), int64_t(100));

    stack.redo();
    QCOMPARE(p.tempo(), 240.0);
    QCOMPARE(t->events()[0].startSample(), int64_t(50));
}


void TestProjectCommands::setTempoCommandMerges() {
    Project p;
    p.setSampleRate(48000);
    p.setTempo(120.0);
    Track* t = p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(100);
    ev.setDurationSample(100);
    t->addEvent(ev);

    UndoStack stack;
    stack.execute(std::make_unique<SetTempoCommand>(p, 120.0, 240.0));
    stack.execute(std::make_unique<SetTempoCommand>(p, 240.0, 120.0)); // merges
    QCOMPARE(p.tempo(), 120.0);
    QCOMPARE(t->events()[0].startSample(), int64_t(100)); // 0.5 then 2.0 -> exact

    // A single undo restores the state before the merged run (tempo 120 / 100)
    stack.undo();
    QCOMPARE(p.tempo(), 120.0);
    QCOMPARE(t->events()[0].startSample(), int64_t(100));
    QVERIFY(!stack.canUndo());
}


void TestProjectCommands::setTimeSigCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<SetTimeSigCommand>(p, 4, 4, 3, 4));
    QCOMPARE(p.timeSigNum(), 3);
    QCOMPARE(p.timeSigDen(), 4);
    stack.undo();
    QCOMPARE(p.timeSigNum(), 4);
    QCOMPARE(p.timeSigDen(), 4);
}



QTEST_MAIN(TestProjectCommands)
#include "test_commands_project.moc"
