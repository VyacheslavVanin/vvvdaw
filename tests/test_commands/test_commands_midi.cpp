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
    void editControlEventsCommand();
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


void TestNoteCommands::editControlEventsCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t eventId = p.tracks()[0].midiEvents()[0].id();
    MidiClip* clip = p.tracks()[0].midiEvents()[0].clip().get();

    // Add two CC1 events in one gesture (a single undo step).
    std::vector<ControlEventChange> adds;
    ControlEventChange a1;
    a1.op = ControlEventChange::Op::Add;
    a1.kind = MidiControlEvent::Kind::ControlChange;
    a1.number = 1;
    a1.newValue = 30;
    a1.newStartTick = 0;
    ControlEventChange a2 = a1;
    a2.newValue = 90;
    a2.newStartTick = 480;
    adds.push_back(a1);
    adds.push_back(a2);

    stack.execute(std::make_unique<EditControlEventsCommand>(p, 0, eventId, std::move(adds)));
    QCOMPARE(clip->controlEvents().size(), size_t(2));
    QCOMPARE(clip->controlEvents()[0].value, 30);
    QCOMPARE(clip->controlEvents()[1].value, 90);
    int64_t id0 = clip->controlEvents()[0].id;
    int64_t id1 = clip->controlEvents()[1].id;
    QVERIFY(id0 != id1);

    // Undo removes the whole gesture; redo restores the same ids.
    stack.undo();
    QCOMPARE(clip->controlEvents().size(), size_t(0));
    stack.redo();
    QCOMPARE(clip->controlEvents().size(), size_t(2));
    QCOMPARE(clip->controlEvents()[0].id, id0);
    QCOMPARE(clip->controlEvents()[1].id, id1);

    // Update one event.
    std::vector<ControlEventChange> upd;
    ControlEventChange u;
    u.op = ControlEventChange::Op::Update;
    u.kind = MidiControlEvent::Kind::ControlChange;
    u.number = 1;
    u.controlEventId = id0;
    u.oldValue = 30;
    u.oldStartTick = 0;
    u.newValue = 55;
    u.newStartTick = 0;
    upd.push_back(u);
    stack.execute(std::make_unique<EditControlEventsCommand>(p, 0, eventId, std::move(upd)));
    QCOMPARE(clip->findControlEvent(id0)->value, 55);
    stack.undo();
    QCOMPARE(clip->findControlEvent(id0)->value, 30);

    // Remove one event; undo restores it with its original values.
    std::vector<ControlEventChange> rm;
    ControlEventChange r;
    r.op = ControlEventChange::Op::Remove;
    r.kind = MidiControlEvent::Kind::ControlChange;
    r.number = 1;
    r.controlEventId = id1;
    r.oldValue = 90;
    r.oldStartTick = 480;
    rm.push_back(r);
    stack.execute(std::make_unique<EditControlEventsCommand>(p, 0, eventId, std::move(rm)));
    QCOMPARE(clip->controlEvents().size(), size_t(1));
    stack.undo();
    QCOMPARE(clip->controlEvents().size(), size_t(2));
    QVERIFY(clip->findControlEvent(id1));
    QCOMPARE(clip->findControlEvent(id1)->value, 90);

    // Pitch bend events are stored with their 14-bit value.
    std::vector<ControlEventChange> pb;
    ControlEventChange b;
    b.op = ControlEventChange::Op::Add;
    b.kind = MidiControlEvent::Kind::PitchBend;
    b.number = 0;
    b.newValue = 4000;
    b.newStartTick = 960;
    pb.push_back(b);
    stack.execute(std::make_unique<EditControlEventsCommand>(p, 0, eventId, std::move(pb)));
    bool foundPb = false;
    for (const auto& e : clip->controlEvents()) {
        if (e.kind == MidiControlEvent::Kind::PitchBend) {
            QCOMPARE(e.value, 4000);
            QCOMPARE(e.startTick, int64_t(960));
            foundPb = true;
        }
    }
    QVERIFY(foundPb);
}


QTEST_MAIN(TestNoteCommands)
#include "test_commands_midi.moc"
