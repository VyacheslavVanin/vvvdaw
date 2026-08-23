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

class TestBusCommands : public QObject {
    Q_OBJECT
private slots:
    void addBusCommand();
    void removeBusCommand();
    void setBusCommands();
    void setBusSendCommands();
    void setBusColorCommand();
    void busFolderCommands();
    void addChannelBusesCommand();
};

void TestBusCommands::addBusCommand() {
    Project p;
    UndoStack stack;
    stack.execute(std::make_unique<AddBusCommand>(p));
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(p.buses()[2].name(), QString("Bus 2"));
    QVERIFY(p.buses()[2].removable());
    stack.undo();
    QCOMPARE(p.buses().size(), size_t(2));
}


void TestBusCommands::removeBusCommand() {
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


void TestBusCommands::setBusCommands() {
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


void TestBusCommands::setBusSendCommands() {
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


void TestBusCommands::setBusColorCommand() {
    Project p;
    UndoStack stack;
    AudioBus folder;
    folder.setName("Folder");
    p.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    p.addBus(std::move(child)); // index 3
    AudioBus grand;
    grand.setName("Grand");
    p.addBus(std::move(grand)); // index 4
    p.busAt(3)->setOutputBusIndex(2);
    p.busAt(4)->setOutputBusIndex(3);

    QColor red("#ff0000");
    QColor blue("#0000ff");

    // Plain assignment to the folder: only it changes; children inherit via
    // propagation (no manual color set on them).
    stack.execute(std::make_unique<SetBusColorCommand>(p, std::vector<SetBusColorCommand::Entry>{
        { 2, QColor(), false, red, true }
    }));
    QVERIFY(p.busAt(2)->colorSet());
    QCOMPARE(p.busAt(2)->color(), red);
    QVERIFY(!p.busAt(3)->colorSet()); // not manually set
    QVERIFY(!p.busAt(4)->colorSet());

    stack.undo();
    QVERIFY(!p.busAt(2)->colorSet());
    stack.redo();
    QVERIFY(p.busAt(2)->colorSet());

    // Ctrl-style override: assign to the folder and clear the manual-color
    // flag on every descendant so they inherit the folder's color (rather than
    // being force-assigned their own copy).
    stack.execute(std::make_unique<SetBusColorCommand>(p, std::vector<SetBusColorCommand::Entry>{
        { 2, red, true, blue, true },
        { 3, QColor(), false, QColor(), false }, // cleared -> inherits
        { 4, QColor(), false, QColor(), false }  // cleared -> inherits
    }));
    QCOMPARE(p.busAt(2)->color(), blue);
    QVERIFY(!p.busAt(3)->colorSet());      // flag cleared
    QVERIFY(!p.busAt(4)->colorSet());
    QCOMPARE(p.busColor(3), blue);          // inherits folder's color
    QCOMPARE(p.busColor(4), blue);          // inherits recursively

    stack.undo();
    QCOMPARE(p.busAt(2)->color(), red);
    QVERIFY(!p.busAt(3)->colorSet());
    QVERIFY(!p.busAt(4)->colorSet());
    stack.redo();
    QVERIFY(!p.busAt(3)->colorSet());
    QCOMPARE(p.busColor(3), blue);

    // Clearing a manual override on a child: giving the child its own color,
    // then a Ctrl-style override clears that flag so it follows the folder.
    p.busAt(3)->setColor(QColor("#123456"));
    QVERIFY(p.busAt(3)->colorSet());
    stack.execute(std::make_unique<SetBusColorCommand>(p, std::vector<SetBusColorCommand::Entry>{
        { 2, blue, true, QColor("#abcdef"), true },
        { 3, QColor("#123456"), true, QColor(), false } // clears child's manual color
    }));
    QCOMPARE(p.busAt(2)->color(), QColor("#abcdef"));
    QVERIFY(!p.busAt(3)->colorSet());
    QCOMPARE(p.busColor(3), QColor("#abcdef")); // inherits the folder
    stack.undo();
    QVERIFY(p.busAt(3)->colorSet());                       // child's manual color restored
    QCOMPARE(p.busAt(3)->color(), QColor("#123456"));
    QCOMPARE(p.busAt(2)->color(), blue);

    // Clearing a manual color (newSet == false).
    stack.execute(std::make_unique<SetBusColorCommand>(p, std::vector<SetBusColorCommand::Entry>{
        { 2, blue, true, QColor(), false }
    }));
    QVERIFY(!p.busAt(2)->colorSet());
    stack.undo();
    QCOMPARE(p.busAt(2)->color(), blue);

    // Out-of-range entries are ignored without crashing.
    stack.execute(std::make_unique<SetBusColorCommand>(p, std::vector<SetBusColorCommand::Entry>{
        { 99, QColor(), false, red, true }
    }));
}


void TestBusCommands::busFolderCommands() {
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


void TestBusCommands::addChannelBusesCommand() {
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


QTEST_MAIN(TestBusCommands)
#include "test_commands_bus.moc"
