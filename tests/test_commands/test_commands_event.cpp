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

class TestEventCommands : public QObject {
    Q_OBJECT
private slots:
    void addEventCommand();
    void removeEventCommand();
    void moveEventCommand();
    void trimEventCommand();
    void trimEventCommandRestoresStart();
    void moveEventToTrackCommand();
    void moveMidiEventToTrackCommand();
    void cutEventCommand();
    void cutEventCommandPreservesRate();
    void cutEventCommandBoundaryDoesNothing();
    void cutEventCommandSnapsBeforeGrid();
    void cutEventCommandSnapsAfterGrid();
    void cutEventCommandSnapOnGrid();
    void setEventsFadeCommand();
    void setEventsFadeCommandClampsToDuration();
    void setEventsFadeCommandLessThanTwoNoOp();
    void cutEventCommandResetsFadesAtSplice();
    void addMidiEventCommand();
    void moveMidiEventCommand();
    void trimMidiEventCommand();
};

static QJsonObject makeEventJson() {
    QJsonObject obj;
    obj["startSample"] = qint64(0);
    obj["offsetSample"] = qint64(0);
    obj["durationSample"] = qint64(100);
    obj["sourceFrames"] = qint64(100);
    return obj;
}

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

void TestEventCommands::addEventCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<AddEventCommand>(p, 0, makeEventJson()));
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].id(), int64_t(1));
    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(0));
}


void TestEventCommands::removeEventCommand() {
    Project p;
    p.addTrack("T");
    p.tracks()[0].addEvent([&] {
        AudioEvent ev;
        ev.setStartSample(10);
        ev.setDurationSample(20);
        return ev;
    }());

    UndoStack stack;
    stack.execute(std::make_unique<RemoveEventCommand>(p, 0, 1));
    QCOMPARE(p.tracks()[0].events().size(), size_t(0));
    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(10));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(20));
}


void TestEventCommands::moveEventCommand() {
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(10);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<MoveEventCommand>(p, 0, id, 10, 500));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(500));
    stack.execute(std::make_unique<MoveEventCommand>(p, 0, id, 500, 600));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(600));
    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(10)); // merged
    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(10));
}


void TestEventCommands::trimEventCommand() {
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(100);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    // Left trim: start and offset advance, duration shrinks.
    stack.execute(std::make_unique<TrimEventCommand>(p, 0, id, 0, 50, 0, 100, 50, 50));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(50));
    QCOMPARE(p.tracks()[0].events()[0].offsetSample(), int64_t(50));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(50));
    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(0));
    QCOMPARE(p.tracks()[0].events()[0].offsetSample(), int64_t(0));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(100));
}


void TestEventCommands::trimEventCommandRestoresStart() {
    // Right-edge trim keeps the start; the start params must still round-trip.
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(1000);
    ev.setOffsetSample(0);
    ev.setDurationSample(200);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<TrimEventCommand>(p, 0, id, 1000, 1000, 0, 200, 0, 120));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(120));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(1000));
    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(200));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(1000));
}


void TestEventCommands::moveEventToTrackCommand() {
    Project p;
    p.addTrack("A1");
    p.addTrack("A2");
    AudioEvent ev;
    ev.setStartSample(100);
    ev.setOffsetSample(10);
    ev.setDurationSample(2000);
    ev.setSourceFrames(3000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<MoveEventToTrackCommand>(p, 0, 1, id, 100, 900));
    QCOMPARE(p.tracks()[0].events().size(), size_t(0));
    QCOMPARE(p.tracks()[1].events().size(), size_t(1));
    const AudioEvent& moved = p.tracks()[1].events()[0];
    QCOMPARE(moved.id(), id);
    QCOMPARE(moved.startSample(), int64_t(900));
    QCOMPARE(moved.offsetSample(), int64_t(10));
    QCOMPARE(moved.durationSample(), int64_t(2000));
    QCOMPARE(moved.sourceFrames(), int64_t(3000));

    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[1].events().size(), size_t(0));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(100));

    stack.redo();
    QCOMPARE(p.tracks()[1].events().size(), size_t(1));
    QCOMPARE(p.tracks()[1].events()[0].startSample(), int64_t(900));
}


void TestEventCommands::moveMidiEventToTrackCommand() {
    Project p;
    p.addMidiTrack("M1");
    p.addMidiTrack("M2");
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 240);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(500);
    ev.setDurationSample(4800);
    p.tracks()[0].addMidiEvent(ev);
    int64_t id = p.tracks()[0].midiEvents()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<MoveEventToTrackCommand>(p, 0, 1, id, 500, 700));
    QCOMPARE(p.tracks()[1].midiEvents().size(), size_t(1));
    QCOMPARE(p.tracks()[0].midiEvents().size(), size_t(0));
    QCOMPARE(p.tracks()[1].midiEvents()[0].startSample(), int64_t(700));
    QVERIFY(p.tracks()[1].midiEvents()[0].clip() == clip);

    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents().size(), size_t(1));
    QCOMPARE(p.tracks()[1].midiEvents().size(), size_t(0));
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(500));

    stack.redo();
    QCOMPARE(p.tracks()[1].midiEvents().size(), size_t(1));
    QCOMPARE(p.tracks()[1].midiEvents()[0].startSample(), int64_t(700));
}


void TestEventCommands::cutEventCommand() {
    Project p;
    p.addTrack("T");
    auto clip = std::make_shared<AudioClip>(std::vector<float>(48000, 0.5f), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(1000);
    ev.setOffsetSample(0);
    ev.setDurationSample(10000);
    ev.setSourceFrames(10000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 4000));
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));

    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.id(), id);                       // original becomes the left part
    QCOMPARE(left.startSample(), int64_t(1000));
    QCOMPARE(left.durationSample(), int64_t(3000)); // cutRel = 4000 - 1000
    QCOMPARE(left.offsetSample(), int64_t(0));
    QCOMPARE(left.sourceFrames(), int64_t(3000));
    QCOMPARE(right.startSample(), int64_t(4000));   // parts fit flush
    QCOMPARE(right.durationSample(), int64_t(7000));
    QCOMPARE(right.offsetSample(), int64_t(3000));
    QCOMPARE(right.sourceFrames(), int64_t(7000));
    QCOMPARE(left.endSample(), right.startSample());
    QVERIFY(left.clip() == right.clip());           // both share the source

    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    const AudioEvent& restored = p.tracks()[0].events()[0];
    QCOMPARE(restored.id(), id);
    QCOMPARE(restored.startSample(), int64_t(1000));
    QCOMPARE(restored.offsetSample(), int64_t(0));
    QCOMPARE(restored.durationSample(), int64_t(10000));
    QCOMPARE(restored.sourceFrames(), int64_t(10000));
    QVERIFY(restored.clip() == clip);

    stack.redo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));
    QCOMPARE(p.tracks()[0].events()[0].endSample(),
             p.tracks()[0].events()[1].startSample());
}


void TestEventCommands::cutEventCommandPreservesRate() {
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(100);
    ev.setDurationSample(1000);
    ev.setSourceFrames(2000); // rate = 2 (time-stretched)
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 400));
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));

    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.durationSample(), int64_t(400));
    QCOMPARE(left.sourceFrames(), int64_t(800));   // cutRel * rate
    QCOMPARE(left.offsetSample(), int64_t(100));
    QCOMPARE(right.startSample(), int64_t(400));
    QCOMPARE(right.durationSample(), int64_t(600));
    QCOMPARE(right.offsetSample(), int64_t(900));   // 100 + 800
    QCOMPARE(right.sourceFrames(), int64_t(1200));  // 2000 - 800
    QCOMPARE(left.endSample(), right.startSample());
}


void TestEventCommands::cutEventCommandBoundaryDoesNothing() {
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(1000);
    ev.setDurationSample(1000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 1000)); // == start
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    stack.undo(); // no-op, must not corrupt the event
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(1000));
}


void TestEventCommands::cutEventCommandSnapsBeforeGrid() {
    // Cut before the nearest grid line (cut = 2600, snap = 3000): only the
    // right piece slides forward to the line, leaving a gap.
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(3000);
    ev.setSourceFrames(3000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 2600, true, 1000.0));
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));

    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.startSample(), int64_t(0));
    QCOMPARE(left.durationSample(), int64_t(2600)); // unchanged
    QCOMPARE(left.sourceFrames(), int64_t(2600));
    QCOMPARE(right.startSample(), int64_t(3000));   // snapped to the grid line
    QCOMPARE(right.durationSample(), int64_t(400));
    QCOMPARE(right.offsetSample(), int64_t(2600));
    QCOMPARE(right.sourceFrames(), int64_t(400));
    QVERIFY(left.endSample() < right.startSample()); // silence gap [2600, 3000)

    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].startSample(), int64_t(0));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(3000));
    stack.redo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));
    QCOMPARE(p.tracks()[0].events()[1].startSample(), int64_t(3000));
}


void TestEventCommands::cutEventCommandSnapsAfterGrid() {
    // Cut after the nearest grid line (cut = 2400, snap = 2000): the left
    // piece is trimmed to the line and the right piece slides left to meet it;
    // audio between the line and the cut is dropped.
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(3000);
    ev.setSourceFrames(3000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 2400, true, 1000.0));
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));

    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.durationSample(), int64_t(2000)); // trimmed to the grid line
    QCOMPARE(left.sourceFrames(), int64_t(2000));
    QCOMPARE(right.startSample(), int64_t(2000));   // flush with the left piece
    QCOMPARE(left.endSample(), right.startSample());
    QCOMPARE(right.durationSample(), int64_t(600));
    QCOMPARE(right.offsetSample(), int64_t(2400));  // keeps its own source window
    QCOMPARE(right.sourceFrames(), int64_t(600));

    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(3000));
    stack.redo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(2000));
    QCOMPARE(p.tracks()[0].events()[1].startSample(), int64_t(2000));
}


void TestEventCommands::cutEventCommandSnapOnGrid() {
    // Cut exactly on a grid line: identical to a plain cut.
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(3000);
    ev.setSourceFrames(3000);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 2000, true, 1000.0));
    QCOMPARE(p.tracks()[0].events().size(), size_t(2));

    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.durationSample(), int64_t(2000));
    QCOMPARE(left.sourceFrames(), int64_t(2000));
    QCOMPARE(right.startSample(), int64_t(2000));
    QCOMPARE(right.offsetSample(), int64_t(2000));
    QCOMPARE(right.durationSample(), int64_t(1000));
    QCOMPARE(right.sourceFrames(), int64_t(1000));
    QCOMPARE(left.endSample(), right.startSample());
}


void TestEventCommands::setEventsFadeCommand() {
    Project p;
    p.addTrack("T");
    AudioEvent e1, e2, e3;
    e1.setStartSample(0);    e1.setDurationSample(4800); e1.setSourceFrames(4800);
    e2.setStartSample(4800); e2.setDurationSample(4800); e2.setSourceFrames(4800);
    e3.setStartSample(9600); e3.setDurationSample(4800); e3.setSourceFrames(4800);
    p.tracks()[0].addEvent(e1);
    int64_t id1 = p.tracks()[0].events()[0].id();
    p.tracks()[0].addEvent(e2);
    int64_t id2 = p.tracks()[0].events()[1].id();
    p.tracks()[0].addEvent(e3);
    int64_t id3 = p.tracks()[0].events()[2].id();

    UndoStack stack;
    // Out-of-order ids: the command orders them by timeline position.
    stack.execute(std::make_unique<SetEventsFadeCommand>(
        p, 0, std::vector<int64_t>{id2, id1, id3}, 240));

    const AudioEvent& ev1 = p.tracks()[0].events()[0];
    const AudioEvent& ev2 = p.tracks()[0].events()[1];
    const AudioEvent& ev3 = p.tracks()[0].events()[2];
    QCOMPARE(ev1.fadeOutSamples(), int64_t(240));   // junction e1-e2
    QCOMPARE(ev2.fadeInSamples(), int64_t(240));
    QCOMPARE(ev2.fadeOutSamples(), int64_t(240));   // junction e2-e3
    QCOMPARE(ev3.fadeInSamples(), int64_t(240));
    QCOMPARE(ev1.fadeInSamples(), int64_t(0));      // no neighbor on the left
    QCOMPARE(ev3.fadeOutSamples(), int64_t(0));     // no neighbor on the right

    stack.undo();
    QCOMPARE(ev1.fadeOutSamples(), int64_t(0));
    QCOMPARE(ev2.fadeInSamples(), int64_t(0));
    QCOMPARE(ev2.fadeOutSamples(), int64_t(0));
    QCOMPARE(ev3.fadeInSamples(), int64_t(0));

    stack.redo();
    QCOMPARE(ev1.fadeOutSamples(), int64_t(240));
    QCOMPARE(ev2.fadeInSamples(), int64_t(240));
    QCOMPARE(ev3.fadeInSamples(), int64_t(240));
}


void TestEventCommands::setEventsFadeCommandClampsToDuration() {
    Project p;
    p.addTrack("T");
    AudioEvent e1, e2;
    e1.setStartSample(0);   e1.setDurationSample(100); e1.setSourceFrames(100);
    e2.setStartSample(100); e2.setDurationSample(50);  e2.setSourceFrames(50);
    p.tracks()[0].addEvent(e1);
    int64_t id1 = p.tracks()[0].events()[0].id();
    p.tracks()[0].addEvent(e2);
    int64_t id2 = p.tracks()[0].events()[1].id();

    UndoStack stack;
    stack.execute(std::make_unique<SetEventsFadeCommand>(
        p, 0, std::vector<int64_t>{id1, id2}, 1000));

    QCOMPARE(p.tracks()[0].events()[0].fadeOutSamples(), int64_t(99)); // duration-1
    QCOMPARE(p.tracks()[0].events()[1].fadeInSamples(), int64_t(49));
}


void TestEventCommands::setEventsFadeCommandLessThanTwoNoOp() {
    Project p;
    p.addTrack("T");
    AudioEvent e1;
    e1.setStartSample(0);
    e1.setDurationSample(4800);
    e1.setFadeOutSamples(12);
    p.tracks()[0].addEvent(e1);
    int64_t id1 = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<SetEventsFadeCommand>(
        p, 0, std::vector<int64_t>{id1}, 240));
    QCOMPARE(p.tracks()[0].events()[0].fadeOutSamples(), int64_t(12)); // untouched

    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].fadeOutSamples(), int64_t(12));
}


void TestEventCommands::cutEventCommandResetsFadesAtSplice() {
    Project p;
    p.addTrack("T");
    auto clip = std::make_shared<AudioClip>(std::vector<float>(48000, 0.5f), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(1000);
    ev.setOffsetSample(0);
    ev.setDurationSample(10000);
    ev.setSourceFrames(10000);
    ev.setFadeInSamples(100);
    ev.setFadeOutSamples(200);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<CutEventCommand>(p, 0, id, 4000));
    const AudioEvent& left = p.tracks()[0].events()[0];
    const AudioEvent& right = p.tracks()[0].events()[1];
    QCOMPARE(left.fadeInSamples(), int64_t(100));   // outer fade kept
    QCOMPARE(left.fadeOutSamples(), int64_t(0));    // splice fade cleared
    QCOMPARE(right.fadeInSamples(), int64_t(0));    // splice fade cleared
    QCOMPARE(right.fadeOutSamples(), int64_t(200)); // outer fade kept

    stack.undo();
    const AudioEvent& restored = p.tracks()[0].events()[0];
    QCOMPARE(restored.fadeInSamples(), int64_t(100));
    QCOMPARE(restored.fadeOutSamples(), int64_t(200));
}



void TestEventCommands::addMidiEventCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    auto cmd = std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson());
    stack.execute(std::move(cmd));
    QCOMPARE(p.tracks()[0].midiEvents().size(), size_t(1));
    QVERIFY(p.tracks()[0].midiEvents()[0].clip());
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents().size(), size_t(0));
}


void TestEventCommands::moveMidiEventCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    int64_t id = p.tracks()[0].midiEvents()[0].id();

    stack.execute(std::make_unique<MoveMidiEventCommand>(p, 0, id, 0, 5000));
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(5000));
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(0));
    stack.redo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(5000));
}


void TestEventCommands::trimMidiEventCommand() {
    Project p;
    p.addMidiTrack("M");
    UndoStack stack;
    stack.execute(std::make_unique<AddMidiEventCommand>(p, 0, makeMidiEventJson()));
    MidiEvent* ev = &p.tracks()[0].midiEvents()[0];
    ev->setStartSample(1000);
    ev->setOffsetSample(0);
    ev->setDurationSample(2000);
    int64_t id = ev->id();

    // Left trim: start and offset advance, duration shrinks.
    stack.execute(std::make_unique<TrimMidiEventCommand>(p, 0, id, 1000, 1300, 0, 2000, 300, 1700));
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(1300));
    QCOMPARE(p.tracks()[0].midiEvents()[0].offsetSample(), int64_t(300));
    QCOMPARE(p.tracks()[0].midiEvents()[0].durationSample(), int64_t(1700));
    stack.undo();
    QCOMPARE(p.tracks()[0].midiEvents()[0].startSample(), int64_t(1000));
    QCOMPARE(p.tracks()[0].midiEvents()[0].offsetSample(), int64_t(0));
    QCOMPARE(p.tracks()[0].midiEvents()[0].durationSample(), int64_t(2000));
}


QTEST_MAIN(TestEventCommands)
#include "test_commands_event.moc"
