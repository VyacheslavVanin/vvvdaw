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

class TestInstrumentCommands : public QObject {
    Q_OBJECT
private slots:
    void addInstrumentCommand();
    void addInstrumentCommandWithSynth();
    void addInstrumentTrackCommand();
    void removeInstrumentCommand();
    void setInstrumentCommands();
    void setInstrumentRoutingCommand();
};

void TestInstrumentCommands::addInstrumentCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<AddInstrumentCommand>(p));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Instrument 1"));
    stack.undo();
    QCOMPARE(p.instruments().size(), size_t(0));
}


void TestInstrumentCommands::addInstrumentCommandWithSynth() {
    // "Add Instrument" now opens the instrument picker and creates the
    // instrument already carrying the chosen synth. The command must create
    // the instrument with a live synth instance, and undo/redo must keep the
    // instrument+synth consistent.
    Project p;
    UndoStack stack;
    QJsonObject synthJson;
    synthJson["type"] = "lv2";
    synthJson["path"] = "http://drumgizmo.org/lv2";

    stack.execute(std::make_unique<AddInstrumentCommand>(
        p, nullptr, 48000, 512, synthJson));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Instrument 1"));
    // If the instrument plugin is not installed the synth slot stays empty;
    // the command itself must still have added the instrument.
    if (!p.instruments()[0].synth())
        QSKIP("DrumGizmo LV2 plugin not installed");
    QVERIFY(p.instruments()[0].synth()->isInstrument());

    stack.undo();
    QCOMPARE(p.instruments().size(), size_t(0));

    stack.redo();
    QCOMPARE(p.instruments().size(), size_t(1));
    QVERIFY(p.instruments()[0].synth() != nullptr);
}


void TestInstrumentCommands::addInstrumentTrackCommand() {
    // Track menu "Add Instrument Track": one atomic undo step creates the
    // instrument (with the chosen synth) and a MIDI track routed to it.
    Project p;
    UndoStack stack;
    QJsonObject synthJson;
    synthJson["type"] = "lv2";
    synthJson["path"] = "http://drumgizmo.org/lv2";

    stack.execute(std::make_unique<AddInstrumentTrackCommand>(
        p, nullptr, 48000, 512, synthJson));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(p.tracks()[0].type(), Track::Type::Midi);
    QCOMPARE(p.tracks()[0].instrumentIndex(), 0);
    QCOMPARE(p.tracks()[0].midiOutputDeviceId(), -1);
    QVERIFY(p.tracks()[0].midiOutputDeviceName().isEmpty());
    if (auto* synth = p.instruments()[0].synth())
        QVERIFY(synth->isInstrument());
    else
        QSKIP("DrumGizmo LV2 plugin not installed");

    stack.undo();
    QCOMPARE(p.instruments().size(), size_t(0));
    QCOMPARE(p.tracks().size(), size_t(0));

    stack.redo();
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(p.tracks()[0].instrumentIndex(), 0);
    QVERIFY(p.instruments()[0].synth() != nullptr);
    QCOMPARE(p.tracks()[0].type(), Track::Type::Midi);
}


void TestInstrumentCommands::removeInstrumentCommand() {
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


void TestInstrumentCommands::setInstrumentCommands() {
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


void TestInstrumentCommands::setInstrumentRoutingCommand() {
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


QTEST_MAIN(TestInstrumentCommands)
#include "test_commands_instrument.moc"
