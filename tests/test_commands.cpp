#include <QTest>
#include <memory>
#include "core/UndoStack.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
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

class TestCommands : public QObject {
    Q_OBJECT
private slots:
    void addTrackCommand();
    void removeTrackCommand();
    void setTrackVolumeCommand();
    void setTrackPanCommand();
    void setTrackMuteSoloCommands();
    void setTrackOutputCommand();
    void setTrackMidiOutputCommand();
    void addBusCommand();
    void removeBusCommand();
    void setBusCommands();
    void setBusSendCommands();
    void busFolderCommands();
    void addInstrumentCommand();
    void removeInstrumentCommand();
    void setInstrumentCommands();
    void setInstrumentRoutingCommand();
    void addChannelBusesCommand();
    void setTempoCommand();
    void setTempoCommandMerges();
    void setTimeSigCommand();
    void addEventCommand();
    void removeEventCommand();
    void moveEventCommand();
    void trimEventCommand();
    void addMidiEventCommand();
    void addNoteCommand();
    void moveNoteCommand();
    void resizeNoteCommand();
    void setNoteVelocityCommand();
    void removeNoteCommand();
    void removeNotesCommand();
    void snapshotCommand();
    void undoStackWithCommands();
};

void TestCommands::addTrackCommand() {
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

void TestCommands::removeTrackCommand() {
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

void TestCommands::setTrackVolumeCommand() {
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

void TestCommands::setTrackPanCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<SetTrackPanCommand>(p, 0, 0.0f, 0.5f));
    QCOMPARE(p.tracks()[0].pan(), 0.5f);
    stack.undo();
    QCOMPARE(p.tracks()[0].pan(), 0.0f);
}

void TestCommands::setTrackMuteSoloCommands() {
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

void TestCommands::setTrackOutputCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<SetTrackOutputCommand>(p, 0, 0, 1));
    QCOMPARE(p.tracks()[0].outputBusIndex(), 1);
    stack.undo();
    QCOMPARE(p.tracks()[0].outputBusIndex(), 0);
}

void TestCommands::setTrackMidiOutputCommand() {
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

void TestCommands::addBusCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<AddBusCommand>(p));
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(p.buses()[2].name(), QString("Bus 2"));
    QVERIFY(p.buses()[2].removable());
    stack.undo();
    QCOMPARE(p.buses().size(), size_t(2));
}

void TestCommands::removeBusCommand() {
    Project p;
    AudioBus bus;
    bus.setName("FX");
    p.addBus(std::move(bus));

    UndoStack stack;
    stack.execute(std::make_unique<RemoveBusCommand>(p, 2));
    QCOMPARE(p.buses().size(), size_t(2));
    stack.undo();
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(p.buses()[2].name(), QString("FX"));
}

void TestCommands::setBusCommands() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<SetBusVolumeCommand>(p, 2, 1.0f, 0.3f));
    stack.execute(std::make_unique<SetBusPanCommand>(p, 2, 0.0f, 0.4f));
    stack.execute(std::make_unique<SetBusMuteCommand>(p, 2, false, true));
    stack.execute(std::make_unique<SetBusSoloCommand>(p, 2, false, true));
    stack.execute(std::make_unique<SetBusNameCommand>(p, 2, QString("Bus 2"), QString("New")));
    stack.execute(std::make_unique<SetBusOutputCommand>(p, 2, 0, 1));

    // Out-of-range bus index must not crash
    stack.execute(std::make_unique<SetBusVolumeCommand>(p, 99, 0.3f, 0.1f));

    while (stack.canUndo()) stack.undo();
    QCOMPARE(p.buses().size(), size_t(2));
}

void TestCommands::setBusSendCommands() {
    Project p;
    UndoStack stack;
    AudioBus fx;
    fx.setName("FX");
    p.addBus(std::move(fx)); // index 2

    // Add a default send (target = master, unity, post-fader).
    stack.execute(std::make_unique<AddBusSendCommand>(p, 2));
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));
    QCOMPARE(p.buses()[2].sends()[0].busIndex, 0);
    QCOMPARE(p.buses()[2].sends()[0].level, 1.0f);
    QCOMPARE(p.buses()[2].sends()[0].preFader, false);

    // Add a second send on an out-of-range bus: no-op.
    stack.execute(std::make_unique<AddBusSendCommand>(p, 99));
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));
    stack.undo(); // undo the no-op
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));

    // Undo the real add removes exactly the appended send.
    stack.undo();
    QCOMPARE(p.buses()[2].sends().size(), size_t(0));
    stack.redo();
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));

    // Set scalar properties on the send.
    stack.execute(std::make_unique<SetBusSendTargetCommand>(p, 2, 0, 0, 1));
    QCOMPARE(p.buses()[2].sends()[0].busIndex, 1);
    stack.execute(std::make_unique<SetBusSendLevelCommand>(p, 2, 0, 1.0f, 0.25f));
    QCOMPARE(p.buses()[2].sends()[0].level, 0.25f);
    stack.execute(std::make_unique<SetBusSendPreCommand>(p, 2, 0, false, true));
    QCOMPARE(p.buses()[2].sends()[0].preFader, true);

    // Out-of-range send index / bus index must not crash.
    stack.execute(std::make_unique<SetBusSendTargetCommand>(p, 2, 5, 0, 1));
    stack.execute(std::make_unique<SetBusSendLevelCommand>(p, 99, 0, 1.0f, 0.5f));

    stack.undo(); // undo the no-op level
    stack.undo(); // undo the no-op target
    stack.undo(); // pre toggle
    QCOMPARE(p.buses()[2].sends()[0].preFader, false);
    stack.undo(); // level
    QCOMPARE(p.buses()[2].sends()[0].level, 1.0f);
    stack.undo(); // target
    QCOMPARE(p.buses()[2].sends()[0].busIndex, 0);

    // Remove the send restores it (with its values) on undo.
    stack.execute(std::make_unique<RemoveBusSendCommand>(p, 2, 0));
    QCOMPARE(p.buses()[2].sends().size(), size_t(0));
    stack.undo();
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));
    QCOMPARE(p.buses()[2].sends()[0].busIndex, 0);

    // Remove on an out-of-range send index: no-op.
    stack.execute(std::make_unique<RemoveBusSendCommand>(p, 2, 3));
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));
    stack.execute(std::make_unique<RemoveBusSendCommand>(p, 99, 0));
    QCOMPARE(p.buses()[2].sends().size(), size_t(1));
}

void TestCommands::busFolderCommands() {
    Project p;
    UndoStack stack;
    AudioBus a;
    a.setName("A");
    p.addBus(std::move(a)); // index 2
    AudioBus b;
    b.setName("B");
    p.addBus(std::move(b)); // index 3

    // Collapse a folder.
    stack.execute(std::make_unique<SetBusFolderCollapsedCommand>(p, 2, false, true));
    QCOMPARE(p.buses()[2].folderCollapsed(), true);
    stack.undo();
    QCOMPARE(p.buses()[2].folderCollapsed(), false);

    // Reorder the display order.
    stack.execute(std::make_unique<ReorderBusesCommand>(
        p, std::vector<int>{ 0, 1, 2, 3 }, std::vector<int>{ 0, 1, 3, 2 }));
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 3, 2 }));
    stack.undo();
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 2, 3 }));

    // Move bus A into bus B (a folder move that also re-routes A's output).
    stack.execute(std::make_unique<MoveBusesCommand>(
        p, std::vector<int>{ 0, 1, 2, 3 }, std::vector<int>{ 0, 1, 3, 2 },
        std::vector<std::pair<int, int>>{ { 2, 0 } },
        std::vector<std::pair<int, int>>{ { 2, 3 } }));
    QCOMPARE(p.buses()[2].outputBusIndex(), 3); // A -> B
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 3, 2 }));
    stack.undo();
    QCOMPARE(p.buses()[2].outputBusIndex(), 0);
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 2, 3 }));

    // Create a folder bus and route the child into it.
    int busCount = static_cast<int>(p.buses().size()); // 4
    stack.execute(std::make_unique<CreateBusFolderCommand>(p, QString("Folder"),
                                                           std::vector<int>{ 2 }));
    QCOMPARE(p.buses().size(), size_t(busCount + 1));
    int folderIdx = static_cast<int>(p.buses().size()) - 1;
    QCOMPARE(p.buses()[folderIdx].name(), QString("Folder"));
    QCOMPARE(p.buses()[2].outputBusIndex(), folderIdx); // A moved into the folder
    QVERIFY(p.isBusFolder(folderIdx));
    stack.undo();
    QCOMPARE(p.buses().size(), size_t(busCount));
    QCOMPARE(p.buses()[2].outputBusIndex(), 0); // A restored to master
}

void TestCommands::addInstrumentCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<AddInstrumentCommand>(p));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Instrument 1"));
    stack.undo();
    QCOMPARE(p.instruments().size(), size_t(0));
}

void TestCommands::removeInstrumentCommand() {
    Project p;
    Instrument inst;
    inst.setName("Pad");
    inst.setVolume(0.4f);
    p.addInstrument(std::move(inst));

    UndoStack stack;
    stack.execute(std::make_unique<RemoveInstrumentCommand>(p, 0));
    QCOMPARE(p.instruments().size(), size_t(0));
    stack.undo();
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Pad"));
    QCOMPARE(p.instruments()[0].volume(), 0.4f);
}

void TestCommands::setInstrumentCommands() {
    Project p;
    Instrument inst;
    p.addInstrument(std::move(inst));

    UndoStack stack;
    stack.execute(std::make_unique<SetInstrumentVolumeCommand>(p, 0, 1.0f, 0.2f));
    stack.execute(std::make_unique<SetInstrumentPanCommand>(p, 0, 0.0f, 0.3f));
    stack.execute(std::make_unique<SetInstrumentMuteCommand>(p, 0, false, true));
    stack.execute(std::make_unique<SetInstrumentSoloCommand>(p, 0, false, true));
    stack.execute(std::make_unique<SetInstrumentOutputCommand>(p, 0, 0, 1));
    stack.execute(std::make_unique<SetInstrumentNameCommand>(p, 0, QString("Instrument 1"), QString("Lead")));

    QCOMPARE(p.instruments()[0].volume(), 0.2f);
    QCOMPARE(p.instruments()[0].pan(), 0.3f);
    QVERIFY(p.instruments()[0].isMuted());
    QVERIFY(p.instruments()[0].isSolo());
    QCOMPARE(p.instruments()[0].outputBusIndex(), 1);
    QCOMPARE(p.instruments()[0].name(), QString("Lead"));

    stack.undo();
    QCOMPARE(p.instruments()[0].name(), QString("Instrument 1"));
    stack.undo();
    QCOMPARE(p.instruments()[0].outputBusIndex(), 0);
    stack.undo();
    QVERIFY(!p.instruments()[0].isSolo());
    stack.undo();
    QVERIFY(!p.instruments()[0].isMuted());
    stack.undo();
    QCOMPARE(p.instruments()[0].pan(), 0.0f);
    stack.undo();
    QCOMPARE(p.instruments()[0].volume(), 1.0f);
}

void TestCommands::setInstrumentRoutingCommand() {
    Project p;
    Instrument inst;
    p.addInstrument(std::move(inst));

    QJsonObject oldRouting = p.instruments()[0].routingToJson();

    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r0;
    r0.busIndex = 1;
    r0.name = "Kick";
    Instrument::ChannelRoute r1;
    r1.busIndex = 0;
    r1.name = "Snare";
    routes.push_back(r0);
    routes.push_back(r1);
    p.instruments()[0].setMultiChannel(true);
    p.instruments()[0].setChannelRoutes(routes);
    QJsonObject newRouting = p.instruments()[0].routingToJson();

    UndoStack stack;
    stack.execute(std::make_unique<SetInstrumentRoutingCommand>(p, 0, oldRouting, newRouting));
    QVERIFY(p.instruments()[0].isMultiChannel());
    QCOMPARE(p.instruments()[0].channelRoutes().size(), size_t(2));

    stack.undo();
    QVERIFY(!p.instruments()[0].isMultiChannel());
    QVERIFY(p.instruments()[0].channelRoutes().empty());

    stack.redo();
    QVERIFY(p.instruments()[0].isMultiChannel());
    QCOMPARE(p.instruments()[0].channelRoutes().size(), size_t(2));
    QCOMPARE(p.instruments()[0].channelRoutes()[0].name, QString("Kick"));
    QCOMPARE(p.instruments()[0].channelRoutes()[1].busIndex, 0);
}

void TestCommands::addChannelBusesCommand() {
    Project p;
    Instrument inst;
    inst.setMultiChannel(true);
    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r0;
    r0.busIndex = 0;
    r0.name = "Ch0";
    routes.push_back(r0);
    inst.setChannelRoutes(routes);
    p.addInstrument(std::move(inst));

    const int busCountBefore = static_cast<int>(p.buses().size()); // 2 (Master, Metronome)
    QJsonObject oldRouting = p.instruments()[0].routingToJson();

    // Simulate the dialog: append two buses and route channel 0/1 to them.
    AudioBus b0;
    b0.setName("Kick");
    p.addBus(std::move(b0));
    AudioBus b1;
    b1.setName("Snare");
    p.addBus(std::move(b1));
    std::vector<Instrument::ChannelRoute> newRoutes;
    Instrument::ChannelRoute nr0;
    nr0.busIndex = busCountBefore;
    nr0.name = "Ch0";
    Instrument::ChannelRoute nr1;
    nr1.busIndex = busCountBefore + 1;
    nr1.name = "Ch1";
    newRoutes.push_back(nr0);
    newRoutes.push_back(nr1);
    p.instruments()[0].setChannelRoutes(newRoutes);

    QJsonObject newRouting = p.instruments()[0].routingToJson();
    QJsonArray createdBuses;
    createdBuses.append(p.buses()[busCountBefore].toJson());
    createdBuses.append(p.buses()[busCountBefore + 1].toJson());

    UndoStack stack;
    stack.push(std::make_unique<AddChannelBusesCommand>(
        p, 0, createdBuses, oldRouting, newRouting));

    // Buses were added live by the dialog; the command records the state.
    QCOMPARE(p.buses().size(), size_t(busCountBefore + 2));
    QCOMPARE(p.instruments()[0].channelRoutes().size(), size_t(2));

    stack.undo();
    QCOMPARE(p.buses().size(), size_t(busCountBefore));
    QCOMPARE(p.instruments()[0].channelRoutes().size(), size_t(1));
    QCOMPARE(p.instruments()[0].channelRoutes()[0].busIndex, 0);

    stack.redo();
    QCOMPARE(p.buses().size(), size_t(busCountBefore + 2));
    QCOMPARE(p.buses()[busCountBefore].name(), QString("Kick"));
    QCOMPARE(p.buses()[busCountBefore + 1].name(), QString("Snare"));
    QCOMPARE(p.instruments()[0].channelRoutes().size(), size_t(2));
    QCOMPARE(p.instruments()[0].channelRoutes()[1].busIndex, busCountBefore + 1);
}

void TestCommands::setTempoCommand() {
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

void TestCommands::setTempoCommandMerges() {
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

void TestCommands::setTimeSigCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<SetTimeSigCommand>(p, 4, 4, 3, 4));
    QCOMPARE(p.timeSigNum(), 3);
    QCOMPARE(p.timeSigDen(), 4);
    stack.undo();
    QCOMPARE(p.timeSigNum(), 4);
    QCOMPARE(p.timeSigDen(), 4);
}

static QJsonObject makeEventJson() {
    QJsonObject obj;
    obj["startSample"] = qint64(0);
    obj["offsetSample"] = qint64(0);
    obj["durationSample"] = qint64(100);
    obj["sourceFrames"] = qint64(100);
    return obj;
}

void TestCommands::addEventCommand() {
    Project p;
    p.addTrack("T");
    UndoStack stack;
    stack.execute(std::make_unique<AddEventCommand>(p, 0, makeEventJson()));
    QCOMPARE(p.tracks()[0].events().size(), size_t(1));
    QCOMPARE(p.tracks()[0].events()[0].id(), int64_t(1));
    stack.undo();
    QCOMPARE(p.tracks()[0].events().size(), size_t(0));
}

void TestCommands::removeEventCommand() {
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

void TestCommands::moveEventCommand() {
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

void TestCommands::trimEventCommand() {
    Project p;
    p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(100);
    p.tracks()[0].addEvent(ev);
    int64_t id = p.tracks()[0].events()[0].id();

    UndoStack stack;
    stack.execute(std::make_unique<TrimEventCommand>(p, 0, id, 0, 100, 50, 50));
    QCOMPARE(p.tracks()[0].events()[0].offsetSample(), int64_t(50));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(50));
    stack.undo();
    QCOMPARE(p.tracks()[0].events()[0].offsetSample(), int64_t(0));
    QCOMPARE(p.tracks()[0].events()[0].durationSample(), int64_t(100));
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

void TestCommands::addMidiEventCommand() {
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

void TestCommands::addNoteCommand() {
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

void TestCommands::moveNoteCommand() {
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

void TestCommands::resizeNoteCommand() {
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

void TestCommands::setNoteVelocityCommand() {
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

void TestCommands::removeNoteCommand() {
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

void TestCommands::removeNotesCommand() {
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

void TestCommands::snapshotCommand() {
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

void TestCommands::undoStackWithCommands() {
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

QTEST_MAIN(TestCommands)
#include "test_commands.moc"
