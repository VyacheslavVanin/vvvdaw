#include <QTest>
#include <QApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <memory>
#include <vector>
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/MidiEvent.h"
#include "model/MidiClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"

class TestBus : public QObject {
    Q_OBJECT
private slots:
    void removeBusRemapsOutputs();
    void removeBusRemapsChannelRoutes();
    void removeBusRemapsSends();
    void busDisplayOrderAndFolders();
    void busFolderHelpers();
    void audioBusFolderCollapsedSerialization();
    void audioBusSerialization();
    void audioBusSendsSerialization();
    void audioBusSidechainSendSerialization();
    void sidechainSendCycleAndRemoval();
    void busColorAutoAndPropagation();
    void busColorSerialization();
    void folderDescendants();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestBus::removeBusRemapsOutputs() {
    Project p;
    AudioBus b1;
    b1.setName("B1");
    p.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    p.addBus(std::move(b2)); // index 3

    Track* t = p.addTrack("T");
    t->setOutputBusIndex(3); // -> B2

    QVERIFY(p.removeBus(2)); // remove B1
    QCOMPARE(p.buses().size(), size_t(3));
    QCOMPARE(t->outputBusIndex(), 2); // shifted down

    Track* t2 = p.addTrack("T2");
    t2->setOutputBusIndex(2); // currently B2
    QVERIFY(p.removeBus(2));
    QCOMPARE(t2->outputBusIndex(), 0); // removed bus remaps to master
}


void TestBus::removeBusRemapsChannelRoutes() {
    Project p;
    AudioBus b1;
    b1.setName("B1");
    p.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    p.addBus(std::move(b2)); // index 3

    Instrument inst;
    inst.setMultiChannel(true);
    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r0;
    r0.busIndex = 3; // -> B2
    r0.name = "Kick";
    Instrument::ChannelRoute r1;
    r1.busIndex = 2; // -> B1
    r1.name = "Snare";
    routes.push_back(r0);
    routes.push_back(r1);
    inst.setChannelRoutes(routes);
    p.addInstrument(std::move(inst));

    QVERIFY(p.removeBus(2)); // remove B1
    const auto& insts = p.instruments();
    QVERIFY(insts[0].isMultiChannel());
    QCOMPARE(insts[0].channelRoutes().size(), size_t(2));
    QCOMPARE(insts[0].channelRoutes()[0].busIndex, 2); // B2 shifted down
    QCOMPARE(insts[0].channelRoutes()[1].busIndex, 0); // removed bus remaps to master
}


void TestBus::removeBusRemapsSends() {
    Project p;
    AudioBus b1;
    b1.setName("B1");
    p.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    p.addBus(std::move(b2)); // index 3
    AudioBus carrier;
    carrier.setName("Carrier");
    p.addBus(std::move(carrier)); // index 4

    AudioBus* c = p.busAt(4);
    std::vector<AudioBus::Send> sends;
    AudioBus::Send s0;
    s0.busIndex = 3; // -> B2
    s0.level = 0.5f;
    s0.preFader = true;
    AudioBus::Send s1;
    s1.busIndex = 2; // -> B1
    s1.level = 1.0f;
    sends.push_back(s0);
    sends.push_back(s1);
    c->setSends(std::move(sends));

    QVERIFY(p.removeBus(2)); // remove B1
    QCOMPARE(p.buses().size(), size_t(4));
    const AudioBus* remapped = p.busAt(3); // carrier shifted down
    QCOMPARE(remapped->name(), QString("Carrier"));
    QCOMPARE(remapped->sends().size(), size_t(2));
    QCOMPARE(remapped->sends()[0].busIndex, 2); // B2 shifted down
    QCOMPARE(remapped->sends()[0].level, 0.5f);
    QCOMPARE(remapped->sends()[0].preFader, true);
    QCOMPARE(remapped->sends()[1].busIndex, 0); // removed bus remaps to master
    QCOMPARE(remapped->sends()[1].level, 1.0f);
    QCOMPARE(remapped->sends()[1].preFader, false);
}


void TestBus::busDisplayOrderAndFolders() {
    Project p;
    AudioBus b1;
    b1.setName("B1");
    p.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    p.addBus(std::move(b2)); // index 3
    AudioBus b3;
    b3.setName("B3");
    p.addBus(std::move(b3)); // index 4

    // Default display order is the natural bus order.
    const auto& order = p.busDisplayOrder();
    QCOMPARE(order, (std::vector<int>{ 0, 1, 2, 3, 4 }));

    // Reorder the display (B2 and B3 swap).
    p.setBusDisplayOrder({ 0, 1, 2, 4, 3 });
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 2, 4, 3 }));

    // Removing a bus drops it from the order and remaps the rest.
    QVERIFY(p.removeBus(3)); // remove B2 (was at 4 in order -> index 3 in vector)
    QCOMPARE(p.buses().size(), size_t(4));
    QCOMPARE(p.busDisplayOrder(), (std::vector<int>{ 0, 1, 2, 3 }));

    // Serialization round trip preserves the order.
    Project copy;
    copy.fromJson(p.toJson());
    QCOMPARE(copy.busDisplayOrder(), p.busDisplayOrder());
}


void TestBus::busFolderHelpers() {
    Project p;
    AudioBus b1;
    b1.setName("Folder");
    p.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("Child");
    p.addBus(std::move(b2)); // index 3
    AudioBus b3;
    b3.setName("Top");
    p.addBus(std::move(b3)); // index 4

    // Nothing is a folder yet (no children).
    QVERIFY(!p.isBusFolder(2));
    QVERIFY(p.folderChildren(2).empty());

    // Route Child (3) into Folder (2); Folder becomes a folder.
    p.busAt(3)->setOutputBusIndex(2);
    QVERIFY(p.isBusFolder(2));
    QCOMPARE(p.folderChildren(2), (std::vector<int>{ 3 }));

    // Top (4) routes to master, so it is top level.
    p.busAt(4)->setOutputBusIndex(0);
    auto top = p.topLevelBusIndices();
    // Master's children / device-routed buses: Top(4). Folder(2) is NOT top
    // level because its child routes into it? Actually Folder routes to master,
    // so it IS top level. Children(3) is not.
    QVERIFY(std::find(top.begin(), top.end(), 4) != top.end());
    QVERIFY(std::find(top.begin(), top.end(), 2) != top.end()); // Folder -> master
    QVERIFY(std::find(top.begin(), top.end(), 3) == top.end()); // child is nested
}


void TestBus::audioBusFolderCollapsedSerialization() {
    AudioBus bus;
    bus.setName("FX");
    bus.setFolderCollapsed(true);
    AudioBus restored = AudioBus::fromJson(bus.toJson());
    QCOMPARE(restored.folderCollapsed(), true);

    // Legacy files without the flag default to unfolded.
    QJsonObject legacy;
    legacy["name"] = "FX";
    QCOMPARE(AudioBus::fromJson(legacy).folderCollapsed(), false);
}


void TestBus::audioBusSerialization() {
    AudioBus bus;
    bus.setName("FX");
    bus.setVolume(0.6f);
    bus.setPan(0.2f);
    bus.setOutputBusIndex(1);
    bus.setSolo(true);
    bus.setMuted(false);
    bus.setRemovable(true);

    AudioBus restored = AudioBus::fromJson(bus.toJson());
    QCOMPARE(restored.name(), QString("FX"));
    QCOMPARE(restored.volume(), 0.6f);
    QCOMPARE(restored.pan(), 0.2f);
    QCOMPARE(restored.outputBusIndex(), 1);
    QCOMPARE(restored.isSolo(), true);
    QCOMPARE(restored.isMuted(), false);
    QCOMPARE(restored.removable(), true);
}


void TestBus::audioBusSendsSerialization() {
    AudioBus bus;
    bus.setName("FX");
    std::vector<AudioBus::Send> sends;
    AudioBus::Send s0;
    s0.busIndex = 2;
    s0.level = 0.3f;
    s0.preFader = false;
    AudioBus::Send s1;
    s1.busIndex = 0;
    s1.level = 1.0f;
    s1.preFader = true;
    sends.push_back(s0);
    sends.push_back(s1);
    bus.setSends(std::move(sends));

    AudioBus restored = AudioBus::fromJson(bus.toJson());
    QCOMPARE(restored.sends().size(), size_t(2));
    QCOMPARE(restored.sends()[0].busIndex, 2);
    QCOMPARE(restored.sends()[0].level, 0.3f);
    QCOMPARE(restored.sends()[0].preFader, false);
    QCOMPARE(restored.sends()[1].busIndex, 0);
    QCOMPARE(restored.sends()[1].level, 1.0f);
    QCOMPARE(restored.sends()[1].preFader, true);

    // A bus with no sends round-trips to an empty list.
    AudioBus plain;
    QCOMPARE(AudioBus::fromJson(plain.toJson()).sends().size(), size_t(0));

    // Legacy files without a "sends" member still load (empty list).
    QJsonObject legacy;
    legacy["name"] = "Legacy";
    legacy["outputBusIndex"] = 0;
    QCOMPARE(AudioBus::fromJson(legacy).sends().size(), size_t(0));
}


void TestBus::audioBusSidechainSendSerialization() {
    AudioBus bus;
    bus.setName("Carrier");
    std::vector<AudioBus::Send> sends;
    AudioBus::Send sc;
    sc.kind = AudioBus::Send::Kind::Sidechain;
    sc.busIndex = 3;
    sc.pluginId = "urn:test:plugin";
    sc.level = 0.25f;
    sc.preFader = true;
    sends.push_back(sc);
    bus.setSends(std::move(sends));

    AudioBus restored = AudioBus::fromJson(bus.toJson());
    QCOMPARE(restored.sends().size(), size_t(1));
    QVERIFY(restored.sends()[0].isSidechain());
    QCOMPARE(restored.sends()[0].busIndex, 3);
    QCOMPARE(restored.sends()[0].sidechainPluginId(), QString("urn:test:plugin"));
    QCOMPARE(restored.sends()[0].level, 0.25f);
    QCOMPARE(restored.sends()[0].preFader, true);

    // Legacy JSON without "kind" loads as a regular bus send.
    QJsonObject legacy;
    legacy["name"] = "Legacy";
    QJsonArray arr;
    QJsonObject s;
    s["bus"] = 2;
    s["level"] = 0.5;
    s["pre"] = true;
    arr.append(s);
    legacy["sends"] = arr;
    AudioBus loaded = AudioBus::fromJson(legacy);
    QCOMPARE(loaded.sends().size(), size_t(1));
    QVERIFY(!loaded.sends()[0].isSidechain());
    QCOMPARE(loaded.sends()[0].busIndex, 2);
}


void TestBus::sidechainSendCycleAndRemoval() {
    Project p;
    AudioBus a;
    a.setName("A");
    p.addBus(std::move(a)); // index 2
    AudioBus b;
    b.setName("B");
    p.addBus(std::move(b)); // index 3

    // Sidechain send on A(2) feeding a plugin in B(3): edge A -> B.
    AudioBus::Send sc;
    sc.kind = AudioBus::Send::Kind::Sidechain;
    sc.busIndex = 3;
    sc.pluginId = "p";
    sc.level = 1.0f;
    p.busAt(2)->sends().push_back(sc);

    // B routes to master, so the edge A -> B does not close a cycle.
    QVERIFY(!wouldCreateBusCycle(p.buses(), 2, 3));
    // Routing B into A closes the sidechain cycle (A -> B and B -> A).
    p.busAt(3)->setOutputBusIndex(2);
    QVERIFY(wouldCreateBusCycle(p.buses(), 2, 3));

    // Helpers resolve the assignment by (target bus, plugin id).
    SidechainSendLocation loc = findSidechainSend(p.buses(), 3, "p");
    QCOMPARE(loc.sourceBus, 2);
    QCOMPARE(loc.sendIndex, 0);
    QCOMPARE(findSidechainSend(p.buses(), 3, "missing").sourceBus, -1);

    // Removing the target bus drops the sidechain send (no master remap).
    QVERIFY(p.removeBus(3));
    QCOMPARE(p.buses().size(), size_t(3));
    QVERIFY(p.busAt(2)->sends().empty());
}


void TestBus::busColorAutoAndPropagation() {
    Project p;
    // Master (0) and Metronome (1) have no assigned color -> alternating auto.
    QCOMPARE(p.busColor(0), QColor("#2e2e2e"));
    QCOMPARE(p.busColor(1), QColor("#333333"));

    AudioBus folder;
    folder.setName("Folder");
    p.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    p.addBus(std::move(child)); // index 3
    AudioBus grandchild;
    grandchild.setName("Grand");
    p.addBus(std::move(grandchild)); // index 4

    // Route Child(3) into Folder(2) and Grand(4) into Child(3). Child becomes a
    // folder (grandchild routes into it).
    p.busAt(3)->setOutputBusIndex(2);
    p.busAt(4)->setOutputBusIndex(3);

    // Auto colors: folder 2 anchors its group with its own tint; child 3 is a
    // folder too (it has a child) so it uses its own tint, not folder 2's.
    QColor folderAuto = p.busColor(2);
    QColor childAuto = p.busColor(3);
    QVERIFY(folderAuto.isValid());
    QVERIFY(folderAuto != QColor("#2e2e2e")); // tinted toward the folder hue
    QVERIFY(folderAuto != childAuto); // each folder gets its own hue
    QCOMPARE(p.busColor(4), childAuto); // grandchild follows child 3's auto

    // Assign a color to the top folder: it and its non-manual descendants
    // propagate it down.
    QColor red("#ff0000");
    p.busAt(2)->setColor(red);
    QCOMPARE(p.busColor(2), red);
    // Child 3 has no manual color -> inherits the folder's red.
    QCOMPARE(p.busColor(3), red);
    // Grandchild 4 inherits through child 3 -> still red.
    QCOMPARE(p.busColor(4), red);

    // Manually assigning a color to a child overrides the propagated one.
    QColor blue("#0000ff");
    p.busAt(3)->setColor(blue);
    QCOMPARE(p.busColor(3), blue);
    QCOMPARE(p.busColor(2), red); // folder unaffected
    QCOMPARE(p.busColor(4), blue); // grandchild now inherits from child

    // Clearing a manual color falls back to the propagated/auto color.
    p.busAt(3)->clearColor();
    QCOMPARE(p.busColor(3), red);
    QCOMPARE(p.busColor(4), red);

    // Out-of-range index yields the fallback gray.
    QCOMPARE(p.busColor(999), QColor("#2e2e2e"));
}


void TestBus::busColorSerialization() {
    AudioBus bus;
    QVERIFY(!bus.colorSet());
    bus.setColor(QColor("#12ab34"));
    QVERIFY(bus.colorSet());
    QCOMPARE(bus.color(), QColor("#12ab34"));

    AudioBus restored = AudioBus::fromJson(bus.toJson());
    QVERIFY(restored.colorSet());
    QCOMPARE(restored.color(), QColor("#12ab34"));

    // A bus without a color round-trips with colorSet() == false.
    AudioBus plain;
    QVERIFY(!AudioBus::fromJson(plain.toJson()).colorSet());

    // Legacy JSON without a "color" member stays unset.
    QJsonObject legacy;
    legacy["name"] = "Legacy";
    QVERIFY(!AudioBus::fromJson(legacy).colorSet());

    // An invalid color string is ignored.
    QJsonObject bad;
    bad["name"] = "Bad";
    bad["color"] = "not-a-color";
    QVERIFY(!AudioBus::fromJson(bad).colorSet());

    // Project round trip preserves bus colors.
    Project p;
    AudioBus f;
    f.setName("Folder");
    f.setColor(QColor("#abcdef"));
    p.addBus(std::move(f));
    Project q;
    q.fromJson(p.toJson());
    QVERIFY(q.buses()[2].colorSet());
    QCOMPARE(q.buses()[2].color(), QColor("#abcdef"));
}


void TestBus::folderDescendants() {
    Project p;
    AudioBus a;
    a.setName("A");
    p.addBus(std::move(a)); // 2
    AudioBus b;
    b.setName("B");
    p.addBus(std::move(b)); // 3
    AudioBus c;
    c.setName("C");
    p.addBus(std::move(c)); // 4
    AudioBus d;
    d.setName("D");
    p.addBus(std::move(d)); // 5

    QVERIFY(p.folderDescendants(2).empty()); // no children yet

    // A(2) -> B(3) -> C(4), and D(5) top level.
    p.busAt(3)->setOutputBusIndex(2);
    p.busAt(4)->setOutputBusIndex(3);
    p.busAt(5)->setOutputBusIndex(0);

    QCOMPARE(p.folderDescendants(2), (std::vector<int>{ 3, 4 }));
    QCOMPARE(p.folderDescendants(3), (std::vector<int>{ 4 }));
    QCOMPARE(p.folderDescendants(4), (std::vector<int>{}));
    QCOMPARE(p.folderDescendants(5), (std::vector<int>{}));
}


QTEST_MAIN(TestBus)
#include "test_model_bus.moc"
