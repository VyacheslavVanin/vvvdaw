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

class TestTrackCommands : public QObject {
    Q_OBJECT
private slots:
    void addTrackCommand();
    void removeTrackCommand();
    void setTrackVolumeCommand();
    void setTrackPanCommand();
    void setTrackMuteSoloCommands();
    void setTrackOutputCommand();
    void setTrackMidiOutputCommand();
    void setTrackHeightCommand();
    void setAllTracksHeightCommand();
    void reorderTracksCommand();
};

void TestTrackCommands::addTrackCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<AddTrackCommand>(p, 0, 2));
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(p.tracks()[0].type(), Track::Type::Audio);
    QCOMPARE(p.tracks()[0].channels(), 2);

    stack.execute(std::make_unique<AddTrackCommand>(p, 1, Track::Type::Midi));
    QCOMPARE(p.tracks().size(), size_t(2));
    QCOMPARE(p.tracks()[1].type(), Track::Type::Midi);

    stack.undo();
    QCOMPARE(p.tracks().size(), size_t(1));
    stack.undo();
    QCOMPARE(p.tracks().size(), size_t(0));
}


void TestTrackCommands::removeTrackCommand() {
    Project p;
    Track* t = p.addTrack("Guitar");
    t->setVolume(0.5f);
    t->setMuted(true);
    p.addMidiTrack("Keys");

    UndoStack stack;
    stack.execute(std::make_unique<RemoveTrackCommand>(p, 0));
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(p.tracks()[0].name(), QString("Keys"));

    stack.undo();
    QCOMPARE(p.tracks().size(), size_t(2));
    QCOMPARE(p.tracks()[0].name(), QString("Guitar"));
    QCOMPARE(p.tracks()[0].type(), Track::Type::Audio);
    QCOMPARE(p.tracks()[0].volume(), 0.5f);
    QCOMPARE(p.tracks()[0].isMuted(), true);
}


void TestTrackCommands::setTrackVolumeCommand() {
    Project p;
    Track* t = p.addTrack("T");
    QCOMPARE(t->volume(), 0.8f);

    UndoStack stack;
    stack.execute(std::make_unique<SetTrackVolumeCommand>(p, 0, 0.8f, 0.5f));
    QCOMPARE(p.tracks()[0].volume(), 0.5f);
    stack.execute(std::make_unique<SetTrackVolumeCommand>(p, 0, 0.5f, 0.3f));
    QCOMPARE(p.tracks()[0].volume(), 0.3f);
    stack.undo();
    QCOMPARE(p.tracks()[0].volume(), 0.8f); // merged back to the original

    // Out-of-range index is a no-op
    stack.execute(std::make_unique<SetTrackVolumeCommand>(p, 7, 0.3f, 0.1f));
    QCOMPARE(p.tracks()[0].volume(), 0.8f);
}


void TestTrackCommands::setTrackPanCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<SetTrackPanCommand>(p, 0, 0.0f, 0.5f));
    QCOMPARE(p.tracks()[0].pan(), 0.5f);
    stack.undo();
    QCOMPARE(p.tracks()[0].pan(), 0.0f);
}


void TestTrackCommands::setTrackMuteSoloCommands() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<SetTrackMuteCommand>(p, 0, false, true));
    QVERIFY(p.tracks()[0].isMuted());
    stack.execute(std::make_unique<SetTrackSoloCommand>(p, 0, false, true));
    QVERIFY(p.tracks()[0].isSolo());
    stack.undo();
    QVERIFY(!p.tracks()[0].isSolo());
    QVERIFY(p.tracks()[0].isMuted());
    stack.undo();
    QVERIFY(!p.tracks()[0].isMuted());
}


void TestTrackCommands::setTrackOutputCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<SetTrackOutputCommand>(p, 0, 0, 1));
    QCOMPARE(p.tracks()[0].outputBusIndex(), 1);
    stack.undo();
    QCOMPARE(p.tracks()[0].outputBusIndex(), 0);
}


void TestTrackCommands::setTrackMidiOutputCommand() {
    Project p;
    p.addMidiTrack("T");
    SetTrackMidiOutputCommand::Routing oldR;
    SetTrackMidiOutputCommand::Routing newR;
    newR.deviceId = 2;
    newR.deviceName = "Out";
    newR.instrumentIndex = 3;

    UndoStack stack;
    stack.execute(std::make_unique<SetTrackMidiOutputCommand>(p, 0, oldR, newR));
    QCOMPARE(p.tracks()[0].midiOutputDeviceId(), 2);
    QCOMPARE(p.tracks()[0].midiOutputDeviceName(), QString("Out"));
    QCOMPARE(p.tracks()[0].instrumentIndex(), 3);
    stack.undo();
    QCOMPARE(p.tracks()[0].midiOutputDeviceId(), -1);
    QCOMPARE(p.tracks()[0].instrumentIndex(), -1);
}


void TestTrackCommands::setTrackHeightCommand() {
    Project p;
    p.addTrack("T");
    QCOMPARE(p.tracks()[0].height(), vvvdaw::DefaultTrackHeight);

    UndoStack stack;
    stack.execute(std::make_unique<SetTrackHeightCommand>(p, 0, vvvdaw::DefaultTrackHeight, 240));
    QCOMPARE(p.tracks()[0].height(), 240);
    stack.undo();
    QCOMPARE(p.tracks()[0].height(), vvvdaw::DefaultTrackHeight);

    // Out-of-range index is a no-op.
    stack.execute(std::make_unique<SetTrackHeightCommand>(p, 5, 240, 300));
    QCOMPARE(p.tracks()[0].height(), vvvdaw::DefaultTrackHeight);
}


void TestTrackCommands::setAllTracksHeightCommand() {
    Project p;
    p.addTrack("A");
    p.addMidiTrack("B");
    p.addTrack("C");
    p.tracks()[0].setHeight(100);
    p.tracks()[1].setHeight(200);
    p.tracks()[2].setHeight(300);

    UndoStack stack;
    std::vector<int> oldHeights = { 100, 200, 300 };
    stack.execute(std::make_unique<SetAllTracksHeightCommand>(p, oldHeights, 220));
    for (auto& t : p.tracks())
        QCOMPARE(t.height(), 220);

    stack.undo();
    QCOMPARE(p.tracks()[0].height(), 100);
    QCOMPARE(p.tracks()[1].height(), 200);
    QCOMPARE(p.tracks()[2].height(), 300);

    stack.redo();
    for (auto& t : p.tracks())
        QCOMPARE(t.height(), 220);
}


void TestTrackCommands::reorderTracksCommand() {
    Project p;
    p.addTrack("A");
    p.addMidiTrack("B");
    p.addTrack("C");
    p.addTrack("D");
    QCOMPARE(p.tracks()[0].name(), QString("A"));
    QCOMPARE(p.tracks()[3].name(), QString("D"));

    UndoStack stack;
    // Move track 0 ("A") to before position 2: A,B,C,D -> B,A,C,D.
    stack.execute(std::make_unique<ReorderTracksCommand>(p,
        std::vector<int>{1, 0, 2, 3}));
    QCOMPARE(p.tracks().size(), size_t(4));
    QCOMPARE(p.tracks()[0].name(), QString("B"));
    QCOMPARE(p.tracks()[1].name(), QString("A"));
    QCOMPARE(p.tracks()[2].name(), QString("C"));
    QCOMPARE(p.tracks()[3].name(), QString("D"));

    stack.undo();
    QCOMPARE(p.tracks()[0].name(), QString("A"));
    QCOMPARE(p.tracks()[1].name(), QString("B"));
    QCOMPARE(p.tracks()[2].name(), QString("C"));
    QCOMPARE(p.tracks()[3].name(), QString("D"));

    stack.redo();
    QCOMPARE(p.tracks()[0].name(), QString("B"));
    QCOMPARE(p.tracks()[1].name(), QString("A"));
}


QTEST_MAIN(TestTrackCommands)
#include "test_commands_track.moc"
