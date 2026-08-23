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

class TestNoteCommands : public QObject {
    Q_OBJECT
private slots:
    void addNoteCommand();
    void moveNoteCommand();
    void resizeNoteCommand();
    void setNoteVelocityCommand();
    void removeNoteCommand();
    void removeNotesCommand();
};

static QJsonObject makeMidiEventJson() {
    MidiClip clip;
    clip.setLengthTicks(4 * MidiClip::kPPQ);
    QJsonObject obj;
    obj["startSample"] = qint64(0);
    obj["offsetSample"] = qint64(0);
    obj["durationSample"] = qint64(75600);
    obj["clip"] = clip.toJson();
    return obj;
}

void TestNoteCommands::addNoteCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();

    stack.execute(std::make_unique<AddNoteCommand>(p, 0, eventId, 60, 100, 0, 240));
    MidiClip* clip = p.tracks()[0].midiEvents()[0].clip().get();
    QCOMPARE(clip->notes().size(), size_t(1));
    QCOMPARE(clip->notes()[0].pitch, 60);
    stack.undo();
    QCOMPARE(clip->notes().size(), size_t(0));
}


void TestNoteCommands::moveNoteCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    stack.execute(std::make_unique<AddNoteCommand>(p, 0, eventId, 60, 100, 0, 240));
    int64_t noteId = p.tracks()[0].midiEvents()[0].clip()->notes()[0].id;

    stack.execute(std::make_unique<MoveNoteCommand>(p, 0, eventId, noteId, 60, 0, 62, 480));
    MidiNote* note = p.tracks()[0].midiEvents()[0].clip()->findNote(noteId);
    QCOMPARE(note->pitch, 62);
    QCOMPARE(note->startTick, int64_t(480));
    stack.undo();
    QCOMPARE(note->pitch, 60);
    QCOMPARE(note->startTick, int64_t(0));
}


void TestNoteCommands::resizeNoteCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    stack.execute(std::make_unique<AddNoteCommand>(p, 0, eventId, 60, 100, 0, 240));
    int64_t noteId = p.tracks()[0].midiEvents()[0].clip()->notes()[0].id;

    stack.execute(std::make_unique<ResizeNoteCommand>(p, 0, eventId, noteId, 240, 480));
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->findNote(noteId)->durationTicks, int64_t(480));
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->findNote(noteId)->durationTicks, int64_t(240));
}


void TestNoteCommands::setNoteVelocityCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    stack.execute(std::make_unique<AddNoteCommand>(p, 0, eventId, 60, 100, 0, 240));
    int64_t noteId = p.tracks()[0].midiEvents()[0].clip()->notes()[0].id;

    stack.execute(std::make_unique<SetNoteVelocityCommand>(p, 0, eventId, noteId, 100, 60));
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->findNote(noteId)->velocity, 60);
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->findNote(noteId)->velocity, 100);
}


void TestNoteCommands::removeNoteCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    stack.execute(std::make_unique<AddNoteCommand>(p, 0, eventId, 60, 100, 0, 240));
    int64_t noteId = p.tracks()[0].midiEvents()[0].clip()->notes()[0].id;

    stack.execute(std::make_unique<RemoveNoteCommand>(p, 0, eventId, noteId));
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->notes().size(), size_t(0));
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->notes().size(), size_t(1));
    QCOMPARE(p.tracks()[0].midiEvents()[0].clip()->notes()[0].pitch, 60);
}


void TestNoteCommands::removeNotesCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    MidiClip* clip = p.tracks()[0].midiEvents()[0].clip().get();
    int64_t n1 = clip->addNote(60, 100, 0, 100);
    int64_t n2 = clip->addNote(64, 90, 200, 100);

    stack.execute(std::make_unique<RemoveNotesCommand>(p, 0, eventId,
                                                       std::vector<int64_t>{n1, n2}));
    QCOMPARE(clip->notes().size(), size_t(0));
    stack.undo();
    QCOMPARE(clip->notes().size(), size_t(2));
    QVERIFY(clip->findNote(n1));
    QVERIFY(clip->findNote(n2));
}


QTEST_MAIN(TestNoteCommands)
#include "test_commands_midi.moc"
