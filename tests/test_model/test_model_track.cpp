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

class TestTrack : public QObject {
    Q_OBJECT
private slots:
    void instrumentSerialization();
    void instrumentRoutingSerialization();
    void trackSerialization();
    void trackHeight();
    void trackPluginPanelWidth();
    void midiChannel();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestTrack::instrumentSerialization() {
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


void TestTrack::instrumentRoutingSerialization() {
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


void TestTrack::trackSerialization() {
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


void TestTrack::trackHeight() {
    Track t("A", 2);
    QCOMPARE(t.height(), vvvdaw::DefaultTrackHeight);

    t.setHeight(220);
    Track rt;
    rt.fromJson(t.toJson());
    QCOMPARE(rt.height(), 220);

    // A legacy track without the height field keeps the default value.
    QJsonObject legacy = t.toJson();
    legacy.remove("height");
    Track legacyTrack;
    legacyTrack.fromJson(legacy);
    QCOMPARE(legacyTrack.height(), vvvdaw::DefaultTrackHeight);
}


void TestTrack::trackPluginPanelWidth() {
    Track t("A", 2);
    QCOMPARE(t.pluginPanelWidth(), vvvdaw::DefaultPluginPanelWidth);

    t.setPluginPanelWidth(340);
    Track rt;
    rt.fromJson(t.toJson());
    QCOMPARE(rt.pluginPanelWidth(), 340);

    // A legacy track without the field keeps the default value.
    QJsonObject legacy = t.toJson();
    legacy.remove("pluginPanelWidth");
    Track legacyTrack;
    legacyTrack.fromJson(legacy);
    QCOMPARE(legacyTrack.pluginPanelWidth(), vvvdaw::DefaultPluginPanelWidth);
}


void TestTrack::midiChannel() {
    Track m("Keys", Track::Type::Midi);
    QCOMPARE(m.midiChannel(), 0); // default channel 1

    m.setMidiChannel(9);
    QCOMPARE(m.midiChannel(), 9);

    // Out-of-range values are clamped to 0..15.
    m.setMidiChannel(-3);
    QCOMPARE(m.midiChannel(), 0);
    m.setMidiChannel(99);
    QCOMPARE(m.midiChannel(), 15);

    m.setMidiChannel(4);
    Track rm;
    rm.fromJson(m.toJson());
    QCOMPARE(rm.midiChannel(), 4);

    // Legacy midi tracks without the field keep channel 0.
    QJsonObject legacy = m.toJson();
    legacy.remove("midiChannel");
    Track legacyTrack;
    legacyTrack.fromJson(legacy);
    QCOMPARE(legacyTrack.midiChannel(), 0);
}


QTEST_MAIN(TestTrack)
#include "test_model_track.moc"
