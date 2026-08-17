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

class TestModel : public QObject {
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
    void midiClipCloneIndependent();
    void midiEventCloneDeepIndependent();
    void audioEventTakes();
    void midiEventTakes();
    void midiClipNotes();
    void midiClipSerialization();
    void audioClipFromSamples();
    void audioClipFileRoundTrip();
    void projectTimeConversions();
    void projectSnapSample();
    void projectRescaleTimeline();
    void projectJsonRoundTrip();
    void projectSaveLoadRoundTrip();
    void projectLoadCreatesMissingBuses();
    void removeBusRemapsOutputs();
    void removeBusRemapsChannelRoutes();
    void removeBusRemapsSends();
    void busDisplayOrderAndFolders();
    void busFolderHelpers();
    void audioBusSerialization();
    void audioBusSendsSerialization();
    void audioBusFolderCollapsedSerialization();
    void instrumentSerialization();
    void instrumentRoutingSerialization();
    void trackSerialization();
    void audioEventSerialization();
    void midiEventSerialization();
};

void TestModel::projectDefaults() {
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

void TestModel::addRemoveTrack() {
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

void TestModel::addRemoveBus() {
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

void TestModel::addRemoveInstrument() {
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

void TestModel::trackEventManagement() {
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

void TestModel::importAssignsUniqueIds() {
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

void TestModel::importAdvancesNextIdCounter() {
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

void TestModel::removeRemovesOnlyMatchingId() {
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

void TestModel::midiClipCloneIndependent() {
    auto orig = std::make_shared<MidiClip>();
    orig->addNote(60, 100, 0, 960);
    orig->addNote(64, 90, 480, 480);
    orig->setLengthTicks(960);

    auto copy = orig->clone();
    QVERIFY(copy.get() != orig.get());
    QCOMPARE(copy->notes().size(), size_t(2));
    QCOMPARE(copy->lengthTicks(), orig->lengthTicks());

    copy->addNote(72, 110, 720, 240);
    QCOMPARE(copy->notes().size(), size_t(3));
    QCOMPARE(orig->notes().size(), size_t(2)); // original untouched
    QCOMPARE(copy->revision(), orig->revision() + 1);
}

void TestModel::midiEventCloneDeepIndependent() {
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(9600);

    auto take = std::make_shared<MidiClip>();
    take->addNote(72, 80, 0, 480);
    ev.addTake(take); // switches the active clip to the take
    QCOMPARE(ev.activeTakeIndex(), 0);
    QCOMPARE(ev.activeClip(), take);

    MidiEvent clone = ev.cloneDeep();
    QVERIFY(clone.activeClip() != ev.activeClip()); // distinct clip objects
    QCOMPARE(clone.activeClip()->notes().size(), size_t(1));
    QCOMPARE(clone.activeTakeIndex(), ev.activeTakeIndex());

    clone.activeClip()->addNote(84, 120, 240, 240);
    QCOMPARE(clone.activeClip()->notes().size(), size_t(2));
    QCOMPARE(ev.activeClip()->notes().size(), size_t(1)); // original unaffected
    QCOMPARE(ev.activeClip(), take);
}

void TestModel::audioEventTakes() {
    AudioEvent ev;
    QVERIFY(!ev.isValid());

    auto main = std::make_shared<AudioClip>(
        std::vector<float>(512, 0.1f), 48000, 1);
    auto take = std::make_shared<AudioClip>(
        std::vector<float>(256, 0.2f), 48000, 1);
    ev.setClip(main);
    ev.setStartSample(10);
    ev.setDurationSample(50);
    QCOMPARE(ev.endSample(), int64_t(60));
    QCOMPARE(ev.activeClip(), main);
    QCOMPARE(ev.activeTakeIndex(), -1);

    ev.addTake(take);
    QCOMPARE(ev.activeTakeIndex(), 0);
    QCOMPARE(ev.activeClip(), take); // adding a take switches to it
    QCOMPARE(ev.takes().size(), size_t(1));
    QVERIFY(ev.isValid());

    ev.setActiveTake(5); // out of range -> ignored
    QCOMPARE(ev.activeTakeIndex(), 0);
    ev.setActiveTake(0);
    QCOMPARE(ev.activeClip(), take);
}

void TestModel::midiEventTakes() {
    MidiEvent ev;
    auto main = std::make_shared<MidiClip>();
    auto take = std::make_shared<MidiClip>();
    ev.setClip(main);
    ev.setStartSample(5);
    ev.setDurationSample(45);
    QCOMPARE(ev.endSample(), int64_t(50));
    QCOMPARE(ev.activeClip(), main);

    ev.addTake(take);
    QCOMPARE(ev.activeTakeIndex(), 0);
    QCOMPARE(ev.activeClip(), take);

    ev.setActiveTake(-1);
    QCOMPARE(ev.activeClip(), take); // invalid index falls back to m_clip
    ev.setActiveTake(0);
    QCOMPARE(ev.activeClip(), take);
}

void TestModel::midiClipNotes() {
    MidiClip clip;
    clip.setLengthTicks(100);
    int64_t id1 = clip.addNote(60, 100, 0, 240);
    int64_t id2 = clip.addNote(64, 80, 960, 240);
    QVERIFY(id1 != id2);
    QCOMPARE(clip.notes().size(), size_t(2));
    QCOMPARE(clip.notes()[0].id, id1);
    QCOMPARE(clip.revision(), int64_t(2));

    // lengthTicks is the max of the stored length and note extents
    QCOMPARE(clip.lengthTicks(), int64_t(1200));

    QVERIFY(clip.findNote(id1));
    QVERIFY(!clip.findNote(999));

    QVERIFY(clip.removeNote(id1));
    QCOMPARE(clip.notes().size(), size_t(1));
    QCOMPARE(clip.revision(), int64_t(3));
    QVERIFY(!clip.removeNote(id1));

    MidiNote n;
    n.id = 1000;
    n.pitch = 70;
    clip.importNote(n);
    QVERIFY(clip.findNote(1000));
}

void TestModel::midiClipSerialization() {
    MidiClip clip;
    clip.setLengthTicks(3840);
    clip.addNote(60, 100, 0, 240);
    clip.addNote(64, 80, 480, 120);

    MidiClip restored;
    restored.fromJson(clip.toJson());
    QCOMPARE(restored.lengthTicks(), clip.lengthTicks());
    QCOMPARE(restored.notes().size(), size_t(2));
    QCOMPARE(restored.notes()[0].pitch, 60);
    QCOMPARE(restored.notes()[0].velocity, 100);
    QCOMPARE(restored.notes()[0].startTick, int64_t(0));
    QCOMPARE(restored.notes()[0].durationTicks, int64_t(240));
    QCOMPARE(restored.notes()[1].id, int64_t(2));

    // Loading old data without nextNoteId must assign fresh ids
    QJsonObject legacy;
    legacy["lengthTicks"] = 10;
    MidiClip legacyClip;
    legacyClip.fromJson(legacy);
    QCOMPARE(legacyClip.notes().size(), size_t(0));
    int64_t id = legacyClip.addNote(50, 90, 0, 10);
    QCOMPARE(id, int64_t(1));
}

void TestModel::audioClipFromSamples() {
    std::vector<float> samples(1024, 0.5f);
    AudioClip clip(std::move(samples), 48000, 1);
    QVERIFY(clip.isValid());
    QCOMPARE(clip.frameCount(), size_t(1024));
    QCOMPARE(clip.channels(), 1);
    QCOMPARE(clip.sampleRate(), 48000);
    QCOMPARE(clip.peaks().size(), size_t(2)); // 1024 / 512 step
    QCOMPARE(clip.peaks()[0].maxAbs, 0.5f);

    AudioClip empty;
    QVERIFY(!empty.isValid());
    QCOMPARE(empty.frameCount(), size_t(0));
}

void TestModel::audioClipFileRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.path() + "/test.wav";

    std::vector<float> samples(2048, 0.25f);
    AudioClip clip(std::move(samples), 44100, 1);
    QVERIFY(clip.saveToFile(path));

    AudioClip loaded(path);
    QVERIFY(loaded.isValid());
    QCOMPARE(loaded.frameCount(), size_t(2048));
    QCOMPARE(loaded.sampleRate(), 44100);
    QCOMPARE(loaded.channels(), 1);
}

void TestModel::projectTimeConversions() {
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

void TestModel::projectSnapSample() {
    Project p;
    p.setSampleRate(48000);
    p.setTempo(120.0); // 24000 samples per beat
    // beatDivision 4 -> snap grid 6000
    QCOMPARE(p.snapSample(0), int64_t(0));
    QCOMPARE(p.snapSample(7000), int64_t(6000));
    QCOMPARE(p.snapSample(13000), int64_t(12000));
    QCOMPARE(p.snapSample(5900), int64_t(6000));
}

void TestModel::projectRescaleTimeline() {
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

void TestModel::projectJsonRoundTrip() {
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

void TestModel::projectSaveLoadRoundTrip() {
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

void TestModel::projectLoadCreatesMissingBuses() {
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

void TestModel::removeBusRemapsOutputs() {
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

void TestModel::removeBusRemapsChannelRoutes() {
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

void TestModel::removeBusRemapsSends() {
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

void TestModel::busDisplayOrderAndFolders() {
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

void TestModel::busFolderHelpers() {
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

void TestModel::audioBusFolderCollapsedSerialization() {
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

void TestModel::audioBusSerialization() {
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

void TestModel::audioBusSendsSerialization() {
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

void TestModel::instrumentSerialization() {
    Instrument inst;
    inst.setName("Lead");
    inst.setVolume(0.4f);
    inst.setPan(-0.3f);
    inst.setOutputBusIndex(2);
    inst.setMuted(true);
    inst.setSolo(false);

    Instrument restored = Instrument::fromJson(inst.toJson());
    QCOMPARE(restored.name(), QString("Lead"));
    QCOMPARE(restored.volume(), 0.4f);
    QCOMPARE(restored.pan(), -0.3f);
    QCOMPARE(restored.outputBusIndex(), 2);
    QCOMPARE(restored.isMuted(), true);
    QCOMPARE(restored.isSolo(), false);
}

void TestModel::instrumentRoutingSerialization() {
    Instrument inst;
    inst.setMultiChannel(true);
    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r0;
    r0.busIndex = 2;
    r0.name = "Kick";
    Instrument::ChannelRoute r1;
    r1.busIndex = 3;
    r1.name = "Snare";
    routes.push_back(r0);
    routes.push_back(r1);
    inst.setChannelRoutes(routes);

    Instrument restored = Instrument::fromJson(inst.toJson());
    QVERIFY(restored.isMultiChannel());
    QCOMPARE(restored.channelRoutes().size(), size_t(2));
    QCOMPARE(restored.channelRoutes()[0].busIndex, 2);
    QCOMPARE(restored.channelRoutes()[0].name, QString("Kick"));
    QCOMPARE(restored.channelRoutes()[1].busIndex, 3);
    QCOMPARE(restored.channelRoutes()[1].name, QString("Snare"));

    // Legacy instruments (no routing block) default to single-bus mode.
    Instrument legacy = Instrument::fromJson(QJsonObject());
    QVERIFY(!legacy.isMultiChannel());
    QVERIFY(legacy.channelRoutes().empty());
}

void TestModel::trackSerialization() {
    Track t("Guitar", 2);
    t.setVolume(0.5f);
    t.setPan(-0.25f);
    t.setMuted(true);
    AudioEvent ev;
    ev.setStartSample(10);
    ev.setDurationSample(20);
    t.addEvent(ev);

    Track rt;
    rt.fromJson(t.toJson());
    QCOMPARE(rt.name(), QString("Guitar"));
    QCOMPARE(rt.type(), Track::Type::Audio);
    QCOMPARE(rt.channels(), 2);
    QCOMPARE(rt.volume(), 0.5f);
    QCOMPARE(rt.pan(), -0.25f);
    QCOMPARE(rt.isMuted(), true);
    QCOMPARE(rt.events().size(), size_t(1));
    QCOMPARE(rt.events()[0].startSample(), int64_t(10));
    QCOMPARE(rt.events()[0].id(), int64_t(1)); // ids re-assigned on load

    Track m("Keys", Track::Type::Midi);
    auto mclip = std::make_shared<MidiClip>();
    mclip->addNote(60, 100, 0, 240);
    MidiEvent mev;
    mev.setClip(mclip);
    mev.setStartSample(0);
    mev.setDurationSample(1000);
    m.addMidiEvent(mev);

    Track rm;
    rm.fromJson(m.toJson());
    QCOMPARE(rm.type(), Track::Type::Midi);
    QCOMPARE(rm.midiEvents().size(), size_t(1));
    QVERIFY(rm.midiEvents()[0].clip());
    QCOMPARE(rm.midiEvents()[0].clip()->notes().size(), size_t(1));
}

void TestModel::audioEventSerialization() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString clipPath = dir.path() + "/clip.wav";
    auto clip = std::make_shared<AudioClip>(std::vector<float>(1024, 0.3f), 44100, 1);
    QVERIFY(clip->saveToFile(clipPath));
    clip->setFilePath(clipPath);

    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(100);
    ev.setOffsetSample(50);
    ev.setDurationSample(200);
    ev.setSourceFrames(1024);
    ev.takes().push_back(clip);

    QJsonObject obj = ev.toJson(dir.path());
    QCOMPARE(obj["clipPath"].toString(), QString("clip.wav")); // stored relative

    AudioEvent restored = AudioEvent::fromJson(obj, dir.path());
    QVERIFY(restored.clip());
    QVERIFY(restored.clip()->isValid());
    QCOMPARE(restored.startSample(), int64_t(100));
    QCOMPARE(restored.offsetSample(), int64_t(50));
    QCOMPARE(restored.durationSample(), int64_t(200));
    QCOMPARE(restored.sourceFrames(), int64_t(1024));
    QCOMPARE(restored.takes().size(), size_t(1));
}

void TestModel::midiEventSerialization() {
    MidiEvent ev;
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 240);
    clip->setLengthTicks(960);
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(1000);

    MidiEvent restored = MidiEvent::fromJson(ev.toJson());
    QVERIFY(restored.clip());
    QCOMPARE(restored.clip()->notes().size(), size_t(1));
    QCOMPARE(restored.clip()->lengthTicks(), int64_t(960));
    QCOMPARE(restored.startSample(), int64_t(0));
    QCOMPARE(restored.durationSample(), int64_t(1000));
}

class TestTemplates : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void builtInNamesAreListed();
    void emptyTemplateHasNoTracksOrInstruments();
    void rockBandTemplateStructure();
    void saveAndLoadUserTemplate();
    void saveTemplateLeavesProjectPathUntouched();
    void saveTemplateRefusesOverwriteWithoutFlag();
    void saveTemplateOverwritesWithFlag();
    void loadTemplateLeavesProjectUnsaved();
    void loadUnknownTemplateFails();
    void sanitizeName();
};

QTemporaryDir* g_templateDir = nullptr;

void TestTemplates::initTestCase() {
    g_templateDir = new QTemporaryDir;
    QVERIFY(g_templateDir->isValid());
    TemplateStore::setTemplatesDirOverride(g_templateDir->path());
    TemplateStore::ensureBuiltInTemplates();
}

void TestTemplates::cleanupTestCase() {
    TemplateStore::setTemplatesDirOverride("");
    delete g_templateDir;
    g_templateDir = nullptr;
}

void TestTemplates::builtInNamesAreListed() {
    QStringList names = TemplateStore::listTemplates();
    QVERIFY(names.contains("empty"));
    QVERIFY(names.contains("rock-band"));
}

void TestTemplates::emptyTemplateHasNoTracksOrInstruments() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "empty"));
    QVERIFY(p.tracks().empty());
    QVERIFY(p.instruments().empty());
    QCOMPARE(p.buses().size(), size_t(2));
    QCOMPARE(p.buses()[0].name(), QString("Master"));
    QCOMPARE(p.buses()[1].name(), QString("Metronome"));
    QVERIFY(p.filePath().isEmpty());
}

void TestTemplates::rockBandTemplateStructure() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "rock-band"));

    QCOMPARE(p.tracks().size(), size_t(6));
    QCOMPARE(p.buses().size(), size_t(4));
    QCOMPARE(p.buses()[2].name(), QString("Guitars"));
    QCOMPARE(p.buses()[3].name(), QString("Drums"));
    QCOMPARE(p.instruments().size(), size_t(1));
    QCOMPARE(p.instruments()[0].name(), QString("Synth"));

    QStringList expected = {"Solo Guitar", "Rhythm Guitar 1", "Rhythm Guitar 2",
                            "Bass", "Drums", "Synth"};
    for (int i = 0; i < 6; ++i)
        QCOMPARE(p.tracks()[i].name(), expected[i]);

    // Guitars and bass route to the Guitars bus, drums to the Drums bus.
    for (int i = 0; i < 4; ++i)
        QCOMPARE(p.tracks()[i].outputBusIndex(), 2);
    QCOMPARE(p.tracks()[4].outputBusIndex(), 3);

    // The synthesizer is a MIDI track routed to the placeholder instrument.
    QCOMPARE(p.tracks()[5].type(), Track::Type::Midi);
    QCOMPARE(p.tracks()[5].instrumentIndex(), 0);
}

void TestTemplates::saveAndLoadUserTemplate() {
    Project source;
    source.addTrack("Drums");
    AudioBus bus;
    bus.setName("FX");
    source.addBus(std::move(bus));

    QVERIFY(TemplateStore::saveTemplate(source, "myband"));
    QVERIFY(TemplateStore::exists("myband"));
    QVERIFY(TemplateStore::listTemplates().contains("myband"));

    Project loaded;
    QVERIFY(TemplateStore::loadTemplate(loaded, "myband"));
    QCOMPARE(loaded.tracks().size(), size_t(1));
    QCOMPARE(loaded.tracks()[0].name(), QString("Drums"));
    QCOMPARE(loaded.buses().size(), size_t(3));
}

void TestTemplates::saveTemplateLeavesProjectPathUntouched() {
    Project source;
    source.setFilePath("/some/project/project.json");
    QVERIFY(TemplateStore::saveTemplate(source, "keep_path"));
    QCOMPARE(source.filePath(), QString("/some/project/project.json"));
}

void TestTemplates::saveTemplateRefusesOverwriteWithoutFlag() {
    Project source;
    source.addTrack("A");
    QVERIFY(TemplateStore::saveTemplate(source, "no_overwrite"));
    QVERIFY(!TemplateStore::saveTemplate(source, "no_overwrite"));
}

void TestTemplates::saveTemplateOverwritesWithFlag() {
    Project source;
    source.addTrack("A");
    QVERIFY(TemplateStore::saveTemplate(source, "with_overwrite"));
    Project modified;
    modified.addTrack("B");
    QVERIFY(TemplateStore::saveTemplate(modified, "with_overwrite", true));

    Project loaded;
    QVERIFY(TemplateStore::loadTemplate(loaded, "with_overwrite"));
    QCOMPARE(loaded.tracks().size(), size_t(1));
    QCOMPARE(loaded.tracks()[0].name(), QString("B"));
}

void TestTemplates::loadTemplateLeavesProjectUnsaved() {
    Project p;
    QVERIFY(TemplateStore::loadTemplate(p, "rock-band"));
    QVERIFY(p.filePath().isEmpty());
    QCOMPARE(p.name(), QString("Untitled"));
}

void TestTemplates::loadUnknownTemplateFails() {
    Project p;
    QVERIFY(!TemplateStore::loadTemplate(p, "does_not_exist"));
}

void TestTemplates::sanitizeName() {
    QCOMPARE(TemplateStore::sanitizeName(" My Band "), QString("My Band"));
    QCOMPARE(TemplateStore::sanitizeName("../evil/name"), QString("evil name"));
    QCOMPARE(TemplateStore::sanitizeName("a b"), QString("a b"));
    QVERIFY(TemplateStore::sanitizeName("..").isEmpty());
    QVERIFY(TemplateStore::sanitizeName("   ").isEmpty());
    QVERIFY(TemplateStore::sanitizeName("/").isEmpty());
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    int status = 0;
    TestModel model;
    status |= QTest::qExec(&model, argc, argv);
    TestTemplates templates;
    status |= QTest::qExec(&templates, argc, argv);
    return status;
}
#include "test_model.moc"
