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

class TestUndoCommands : public QObject {
    Q_OBJECT
private slots:
    void snapshotCommand();
    void undoStackWithCommands();
};

void TestUndoCommands::snapshotCommand() {
    Project p;
    p.addTrack("One");

    UndoStack stack;
    stack.execute(std::make_unique<SnapshotCommand>(p));
    p.addTrack("Two");
    QCOMPARE(p.tracks().size(), size_t(2));

    stack.undo();
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(p.tracks()[0].name(), QString("One"));

    stack.redo();
    QCOMPARE(p.tracks().size(), size_t(2));
}


void TestUndoCommands::undoStackWithCommands() {
    Project p;
    p.addTrack("T");
    UndoStack stack;

    stack.execute(std::make_unique<SetTrackVolumeCommand>(p, 0, 0.8f, 0.5f));
    stack.execute(std::make_unique<SetTrackMuteCommand>(p, 0, false, true));
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());

    stack.undo();
    QVERIFY(!p.tracks()[0].isMuted());
    stack.undo();
    QCOMPARE(p.tracks()[0].volume(), 0.8f);
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.canRedo());

    stack.redo();
    QCOMPARE(p.tracks()[0].volume(), 0.5f);
    stack.redo();
    QVERIFY(p.tracks()[0].isMuted());
}


QTEST_MAIN(TestUndoCommands)
#include "test_commands_undo.moc"
