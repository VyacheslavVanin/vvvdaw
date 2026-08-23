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

class TestProject : public QObject {
    Q_OBJECT
private slots:
    void projectDefaults();
    void addRemoveTrack();
    void addRemoveBus();
    void addRemoveInstrument();
    void trackEventManagement();
    void importAssignsUniqueIds();
    void importAdvancesNextIdCounter();
    void removeRemovesOnlyMatchingId();
    void projectTimeConversions();
    void projectSnapSample();
    void projectRescaleTimeline();
    void projectJsonRoundTrip();
    void projectSaveLoadRoundTrip();
    void projectLoadCreatesMissingBuses();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestProject::projectDefaults() {
    Project p;
    QCOMPARE(p.name(), QString("Untitled"));
    QCOMPARE(p.tempo(), 120.0);
    QCOMPARE(p.timeSigNum(), 4);
    QCOMPARE(p.timeSigDen(), 4);
    QCOMPARE(p.tracks().size(), size_t(0));
    QCOMPARE(p.buses().size(), size_t(2));
    QCOMPARE(p.buses()[0].name(), QString("Master"));
    QCOMPARE(p.buses()[0].removable(), false);
    QCOMPARE(p.buses()[0].outputBusIndex(), -1);
    QCOMPARE(p.buses()[1].name(), QString("Metronome"));
    QCOMPARE(p.buses()[1].removable(), false);
    QVERIFY(!p.hasLoop());
    QVERIFY(!p.hasRecordRegion());
}


void TestProject::addRemoveTrack() {
    Project p;
    Track* t = p.addTrack("A");
    QVERIFY(t);
    QCOMPARE(p.tracks().size(), size_t(1));
    QCOMPARE(t->name(), QString("A"));
    QCOMPARE(t->type(), Track::Type::Audio);

    Track* m = p.addMidiTrack("B");
    QVERIFY(m);
    QCOMPARE(m->type(), Track::Type::Midi);
    QCOMPARE(p.tracks().size(), size_t(2));

    QVERIFY(!p.removeTrack(5));
    QVERIFY(!p.removeTrack(-1));
    QVERIFY(p.removeTrack(1));
    QCOMPARE(p.tracks().size(), size_t(1));
}


void TestProject::addRemoveBus() {
    Project p;
    AudioBus bus;
    bus.setName("Drums");
    bus.setVolume(0.5f);
    int idx = p.addBus(std::move(bus));
    QCOMPARE(idx, 2);
    QCOMPARE(p.buses().size(), size_t(3));

    // Master (index 0) and Metronome (index 1) are not removable.
    QVERIFY(!p.removeBus(0));
    QVERIFY(!p.removeBus(1));
    QVERIFY(p.removeBus(2));
    QCOMPARE(p.buses().size(), size_t(2));
}


void TestProject::addRemoveInstrument() {
    Project p;
    Track* t = p.addTrack("Track");
    t->setInstrumentIndex(0);

    Instrument inst;
    inst.setName("Pad");
    int idx = p.addInstrument(std::move(inst));
    QCOMPARE(idx, 0);

    QVERIFY(!p.removeInstrument(3));
    QVERIFY(p.removeInstrument(0));
    QCOMPARE(p.instruments().size(), size_t(0));
    QCOMPARE(t->instrumentIndex(), -1);
}


void TestProject::trackEventManagement() {
    Track t("T", 2);

    AudioEvent e1;
    e1.setStartSample(0);
    AudioEvent e2;
    e2.setStartSample(100);
    t.addEvent(e1);
    t.addEvent(e2);
    QCOMPARE(t.events().size(), size_t(2));
    QCOMPARE(t.events()[0].id(), int64_t(1)); // ids assigned sequentially
    QCOMPARE(t.events()[1].id(), int64_t(2));

    t.removeEvent(1);
    QCOMPARE(t.events().size(), size_t(1));
    QCOMPARE(t.events()[0].id(), int64_t(2));
    QVERIFY(t.findEvent(2));
    QVERIFY(!t.findEvent(999));

    AudioEvent imported;
    imported.setId(42);
    imported.setStartSample(200);
    t.importEvent(imported);
    QCOMPARE(t.events().back().id(), int64_t(42)); // import preserves id

    MidiEvent me;
    t.addMidiEvent(me);
    QCOMPARE(t.midiEvents().size(), size_t(1));
    QCOMPARE(t.midiEvents()[0].id(), int64_t(1));
    t.removeMidiEvent(1);
    QVERIFY(t.midiEvents().empty());
}


void TestProject::importAssignsUniqueIds() {
    Track t("T", 2);
    AudioEvent e1;
    e1.setId(1);
    t.importEvent(e1);
    AudioEvent e2;
    e2.setId(1); // collides with the existing id 1 -> must be reassigned
    t.importEvent(e2);
    QCOMPARE(t.events().size(), size_t(2));
    QVERIFY(t.events()[0].id() != t.events()[1].id());

    Track m("M", Track::Type::Midi);
    MidiEvent me1;
    me1.setId(1);
    m.importMidiEvent(me1);
    MidiEvent me2;
    me2.setId(1);
    m.importMidiEvent(me2);
    QCOMPARE(m.midiEvents().size(), size_t(2));
    QVERIFY(m.midiEvents()[0].id() != m.midiEvents()[1].id());
}


void TestProject::importAdvancesNextIdCounter() {
    Track t("T", 2);
    AudioEvent high;
    high.setId(42);
    t.importEvent(high);
    QCOMPARE(t.events()[0].id(), int64_t(42)); // preserved, no collision
    AudioEvent next;
    t.addEvent(next);
    QVERIFY(t.events().back().id() > 42);

    Track m("M", Track::Type::Midi);
    MidiEvent mHigh;
    mHigh.setId(42);
    m.importMidiEvent(mHigh);
    QCOMPARE(m.midiEvents()[0].id(), int64_t(42));
    MidiEvent mNext;
    m.addMidiEvent(mNext);
    QVERIFY(m.midiEvents().back().id() > 42);
}


void TestProject::removeRemovesOnlyMatchingId() {
    Track m("M", Track::Type::Midi);
    MidiEvent a;
    a.setId(1);
    a.setStartSample(0);
    m.importMidiEvent(a);
    MidiEvent b;
    b.setId(2);
    b.setStartSample(100);
    m.importMidiEvent(b);
    MidiEvent c;
    c.setId(1); // collides -> gets a fresh id, sibling id 1 must be safe
    c.setStartSample(200);
    m.importMidiEvent(c);
    QCOMPARE(m.midiEvents().size(), size_t(3));

    const int64_t victimId = m.midiEvents()[0].id();
    m.removeMidiEvent(victimId);
    QCOMPARE(m.midiEvents().size(), size_t(2)); // only the matching event is removed
    for (const auto& ev : m.midiEvents())
        QVERIFY(ev.id() != victimId);
}


void TestProject::projectTimeConversions() {
    Project p;
    p.setSampleRate(48000);
    p.setTempo(120.0);
    QCOMPARE(p.samplesPerBeat(), 24000.0);
    QCOMPARE(p.samplesPerBar(), 96000.0); // 4/4
    QCOMPARE(p.samplesPerTick(), 25.0);   // 24000 / 960 ppq
    QCOMPARE(p.ticksToSamples(1), int64_t(25));
    QCOMPARE(p.ticksToSamples(100), int64_t(2500));
    QCOMPARE(p.samplesToTicks(25), int64_t(1));
    QCOMPARE(p.samplesToTicks(37), int64_t(1)); // rounds
    QCOMPARE(p.samplesToTicks(75), int64_t(3));

    p.setTempo(140.0);
    QCOMPARE(p.samplesPerBeat(), 48000.0 * 60.0 / 140.0);
}


void TestProject::projectSnapSample() {
    Project p;
    p.setSampleRate(48000);
    p.setTempo(120.0); // 24000 samples per beat
    // beatDivision 4 -> snap grid 6000
    QCOMPARE(p.snapSample(0), int64_t(0));
    QCOMPARE(p.snapSample(7000), int64_t(6000));
    QCOMPARE(p.snapSample(13000), int64_t(12000));
    QCOMPARE(p.snapSample(5900), int64_t(6000));
}


void TestProject::projectRescaleTimeline() {
    Project p;
    p.setLoop(100, 200);
    p.setRecordRegion(1000, 2000);
    Track* t = p.addTrack("T");
    AudioEvent ev;
    ev.setStartSample(100);
    ev.setDurationSample(50);
    t->addEvent(ev);

    p.rescaleTimeline(2.0);
    QCOMPARE(p.loopStart(), int64_t(200));
    QCOMPARE(p.loopEnd(), int64_t(400));
    QCOMPARE(p.recordRegionStart(), int64_t(2000));
    QCOMPARE(p.recordRegionEnd(), int64_t(4000));
    QCOMPARE(t->events()[0].startSample(), int64_t(200));
    QCOMPARE(t->events()[0].durationSample(), int64_t(100));

    p.rescaleTimeline(1.0); // no-op
    QCOMPARE(t->events()[0].startSample(), int64_t(200));
    p.rescaleTimeline(0.0); // no-op
    QCOMPARE(t->events()[0].startSample(), int64_t(200));
}


void TestProject::projectJsonRoundTrip() {
    Project p;
    p.setName("JsonTest");
    p.setTempo(133.0);
    p.setTimeSignature(7, 8);
    p.setLoop(111, 222);
    p.setRecordRegion(333, 444);

    Track* t = p.addTrack("Guitar", 2);
    t->setVolume(0.42f);
    t->setPan(-0.3f);
    t->setMuted(true);
    AudioEvent ev;
    ev.setStartSample(10);
    ev.setOffsetSample(2);
    ev.setDurationSample(64);
    ev.setSourceFrames(128);
    t->addEvent(ev);

    AudioBus bus;
    bus.setName("FX");
    bus.setVolume(0.5f);
    bus.setPan(0.25f);
    bus.setOutputBusIndex(0);
    p.addBus(std::move(bus));

    Instrument inst;
    inst.setName("Synth");
    inst.setVolume(0.9f);
    inst.setPan(-0.5f);
    inst.setOutputBusIndex(1);
    inst.setSolo(true);
    p.addInstrument(std::move(inst));

    Project q;
    q.fromJson(p.toJson());
    QCOMPARE(q.name(), p.name());
    QCOMPARE(q.tempo(), 133.0);
    QCOMPARE(q.timeSigNum(), 7);
    QCOMPARE(q.timeSigDen(), 8);
    QCOMPARE(q.loopStart(), int64_t(111));
    QCOMPARE(q.loopEnd(), int64_t(222));
    QCOMPARE(q.recordRegionStart(), int64_t(333));
    QCOMPARE(q.recordRegionEnd(), int64_t(444));

    QCOMPARE(q.tracks().size(), size_t(1));
    QCOMPARE(q.tracks()[0].name(), QString("Guitar"));
    QCOMPARE(q.tracks()[0].channels(), 2);
    QCOMPARE(q.tracks()[0].volume(), 0.42f);
    QCOMPARE(q.tracks()[0].pan(), -0.3f);
    QCOMPARE(q.tracks()[0].isMuted(), true);
    QCOMPARE(q.tracks()[0].events().size(), size_t(1));
    QCOMPARE(q.tracks()[0].events()[0].startSample(), int64_t(10));
    QCOMPARE(q.tracks()[0].events()[0].offsetSample(), int64_t(2));
    QCOMPARE(q.tracks()[0].events()[0].durationSample(), int64_t(64));
    QCOMPARE(q.tracks()[0].events()[0].sourceFrames(), int64_t(128));

    QCOMPARE(q.buses().size(), size_t(3));
    QCOMPARE(q.buses()[0].name(), QString("Master"));
    QCOMPARE(q.buses()[0].removable(), false);
    QCOMPARE(q.buses()[1].name(), QString("Metronome"));
    QCOMPARE(q.buses()[2].name(), QString("FX"));
    QCOMPARE(q.buses()[2].volume(), 0.5f);
    QCOMPARE(q.buses()[2].pan(), 0.25f);

    QCOMPARE(q.instruments().size(), size_t(1));
    QCOMPARE(q.instruments()[0].name(), QString("Synth"));
    QCOMPARE(q.instruments()[0].volume(), 0.9f);
    QCOMPARE(q.instruments()[0].pan(), -0.5f);
    QCOMPARE(q.instruments()[0].outputBusIndex(), 1);
    QCOMPARE(q.instruments()[0].isSolo(), true);
}


void TestProject::projectSaveLoadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString projPath = dir.path() + "/roundtrip.vvvdaw";

    Project p;
    p.setName("RoundTrip");
    p.setTempo(140.0);
    p.setTimeSignature(3, 4);

    Track* audio = p.addTrack("Guitar");
    audio->setVolume(0.5f);
    audio->setPan(-0.25f);
    audio->setMuted(true);

    auto clip = std::make_shared<AudioClip>(
        std::vector<float>(4096, 0.1f), 44100, 1);
    auto take = std::make_shared<AudioClip>(
        std::vector<float>(4096, 0.2f), 44100, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(1000);
    ev.setOffsetSample(0);
    ev.setDurationSample(2048);
    ev.setSourceFrames(4096);
    ev.takes().push_back(take);
    audio->addEvent(ev);

    Track* midi = p.addMidiTrack("Keys");
    auto mclip = std::make_shared<MidiClip>();
    mclip->setLengthTicks(4 * MidiClip::kPPQ);
    mclip->addNote(60, 100, 0, 240);
    mclip->addNote(64, 80, 480, 120);
    MidiEvent mev;
    mev.setClip(mclip);
    mev.setStartSample(0);
    mev.setOffsetSample(0);
    mev.setDurationSample(75600);
    midi->addMidiEvent(mev);

    AudioBus bus;
    bus.setName("Drums");
    bus.setVolume(0.7f);
    bus.setPan(0.1f);
    bus.setOutputBusIndex(0);
    p.addBus(std::move(bus));

    Instrument inst;
    inst.setName("Pad");
    inst.setVolume(0.4f);
    inst.setPan(0.2f);
    inst.setOutputBusIndex(0);
    inst.setMuted(true);
    p.addInstrument(std::move(inst));

    QVERIFY(p.save(projPath));

    Project q;
    QVERIFY(q.load(projPath));
    QCOMPARE(q.name(), QString("RoundTrip"));
    QCOMPARE(q.tempo(), 140.0);
    QCOMPARE(q.timeSigNum(), 3);
    QCOMPARE(q.timeSigDen(), 4);

    QCOMPARE(q.tracks().size(), size_t(2));
    QCOMPARE(q.tracks()[0].name(), QString("Guitar"));
    QCOMPARE(q.tracks()[0].type(), Track::Type::Audio);
    QCOMPARE(q.tracks()[0].volume(), 0.5f);
    QCOMPARE(q.tracks()[0].pan(), -0.25f);
    QCOMPARE(q.tracks()[0].isMuted(), true);
    QCOMPARE(q.tracks()[0].events().size(), size_t(1));
    const AudioEvent& rev = q.tracks()[0].events()[0];
    QCOMPARE(rev.startSample(), int64_t(1000));
    QCOMPARE(rev.durationSample(), int64_t(2048));
    QCOMPARE(rev.sourceFrames(), int64_t(4096));
    QVERIFY(rev.clip());
    QVERIFY(rev.clip()->isValid());
    QVERIFY(rev.clip()->filePath().endsWith(".wav"));
    QCOMPARE(rev.takes().size(), size_t(1));
    QVERIFY(rev.takes()[0]->isValid());
    QCOMPARE(rev.activeTakeIndex(), -1);

    QCOMPARE(q.tracks()[1].name(), QString("Keys"));
    QCOMPARE(q.tracks()[1].type(), Track::Type::Midi);
    QCOMPARE(q.tracks()[1].midiEvents().size(), size_t(1));
    const MidiEvent& rm = q.tracks()[1].midiEvents()[0];
    QVERIFY(rm.clip());
    QCOMPARE(rm.clip()->lengthTicks(), int64_t(4 * MidiClip::kPPQ));
    QCOMPARE(rm.clip()->notes().size(), size_t(2));
    QCOMPARE(rm.clip()->notes()[0].pitch, 60);
    QCOMPARE(rm.clip()->notes()[1].pitch, 64);

    QCOMPARE(q.buses().size(), size_t(3));
    QCOMPARE(q.buses()[0].name(), QString("Master"));
    QCOMPARE(q.buses()[0].removable(), false);
    QCOMPARE(q.buses()[1].name(), QString("Metronome"));
    QCOMPARE(q.buses()[1].removable(), false);
    QCOMPARE(q.buses()[2].name(), QString("Drums"));
    QCOMPARE(q.buses()[2].volume(), 0.7f);
    QCOMPARE(q.buses()[2].pan(), 0.1f);

    QCOMPARE(q.instruments().size(), size_t(1));
    QCOMPARE(q.instruments()[0].name(), QString("Pad"));
    QCOMPARE(q.instruments()[0].volume(), 0.4f);
    QCOMPARE(q.instruments()[0].pan(), 0.2f);
    QCOMPARE(q.instruments()[0].isMuted(), true);
}


void TestProject::projectLoadCreatesMissingBuses() {
    // A project file with no buses must still end up with Master + Metronome.
    QJsonObject obj;
    obj["name"] = "Legacy";
    obj["tempo"] = 90.0;
    obj["tracks"] = QJsonArray();

    Project p;
    p.fromJson(obj);
    QCOMPARE(p.buses().size(), size_t(2));
    QCOMPARE(p.buses()[0].name(), QString("Master"));
    QCOMPARE(p.buses()[0].removable(), false);
    QCOMPARE(p.buses()[1].name(), QString("Metronome"));
    QCOMPARE(p.buses()[1].removable(), false);
}


QTEST_MAIN(TestProject)
#include "test_model_project.moc"
