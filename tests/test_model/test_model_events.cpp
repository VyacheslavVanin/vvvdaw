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

class TestEvents : public QObject {
    Q_OBJECT
private slots:
    void midiEventCloneDeepIndependent();
    void audioEventTakes();
    void midiEventTakes();
    void audioEventSerialization();
    void audioEventFadesDefaultToZero();
    void midiEventSerialization();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestEvents::midiEventCloneDeepIndependent() {
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


void TestEvents::audioEventTakes() {
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


void TestEvents::midiEventTakes() {
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


void TestEvents::audioEventSerialization() {
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
    ev.setFadeInSamples(30);
    ev.setFadeOutSamples(40);
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
    QCOMPARE(restored.fadeInSamples(), int64_t(30));
    QCOMPARE(restored.fadeOutSamples(), int64_t(40));
    QCOMPARE(restored.takes().size(), size_t(1));
}


void TestEvents::audioEventFadesDefaultToZero() {
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(1000);
    QCOMPARE(ev.fadeInSamples(), int64_t(0));
    QCOMPARE(ev.fadeOutSamples(), int64_t(0));

    // Old project files without fade fields round-trip to zero fades.
    AudioEvent restored = AudioEvent::fromJson(ev.toJson());
    QCOMPARE(restored.fadeInSamples(), int64_t(0));
    QCOMPARE(restored.fadeOutSamples(), int64_t(0));
}


void TestEvents::midiEventSerialization() {
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


QTEST_MAIN(TestEvents)
#include "test_model_events.moc"
