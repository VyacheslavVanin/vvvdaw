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

class TestClips : public QObject {
    Q_OBJECT
private slots:
    void audioClipFromSamples();
    void audioClipPeaksSignedMinMax();
    void audioClipReadFrames();
    void streamingClipReadFramesFromFile();
    void audioClipFileRoundTrip();
    void midiClipCloneIndependent();
    void midiClipNotes();
    void midiClipSerialization();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void TestClips::audioClipFromSamples() {
    std::vector<float> samples(1024, 0.5f);
    AudioClip clip(std::move(samples), 48000, 1);
    QVERIFY(clip.isValid());
    QCOMPARE(clip.frameCount(), size_t(1024));
    QCOMPARE(clip.channels(), 1);
    QCOMPARE(clip.sampleRate(), 48000);
    QCOMPARE(clip.peaks().size(), size_t(2)); // 1024 / 512 step
    QCOMPARE(clip.finePeaks().size(), size_t(64)); // 1024 / 16 step
    QCOMPARE(clip.peaks()[0].min, 0.5f);
    QCOMPARE(clip.peaks()[0].max, 0.5f);

    AudioClip empty;
    QVERIFY(!empty.isValid());
    QCOMPARE(empty.frameCount(), size_t(0));
}


void TestClips::audioClipPeaksSignedMinMax() {
    // A window whose first half is -0.8 and second half +0.3 must produce a
    // peak with min=-0.8 and max=+0.3 (signed, not absolute).
    std::vector<float> samples(AudioClip::FINE_PEAK_STEP_FRAMES, 0.0f);
    const size_t half = AudioClip::FINE_PEAK_STEP_FRAMES / 2;
    for (size_t i = 0; i < half; ++i)
        samples[i] = -0.8f;
    for (size_t i = half; i < AudioClip::FINE_PEAK_STEP_FRAMES; ++i)
        samples[i] = 0.3f;
    AudioClip clip(std::move(samples), 48000, 1);

    QCOMPARE(clip.finePeaks().size(), size_t(1));
    QVERIFY(std::abs(clip.finePeaks()[0].min - (-0.8f)) < 1e-6f);
    QVERIFY(std::abs(clip.finePeaks()[0].max - 0.3f) < 1e-6f);

    // The coarse level folds 32 fine peaks; here one fine peak is folded.
    QCOMPARE(clip.peaks().size(), size_t(1));
    QVERIFY(std::abs(clip.peaks()[0].min - (-0.8f)) < 1e-6f);
    QVERIFY(std::abs(clip.peaks()[0].max - 0.3f) < 1e-6f);
}


void TestClips::audioClipReadFrames() {
    std::vector<float> samples(4096, 0.0f);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>(i % 7) - 3.0f;
    AudioClip clip(std::move(samples), 48000, 1);

    std::vector<float> out;
    QVERIFY(clip.readFrames(100, 512, out));
    QCOMPARE(out.size(), size_t(512));
    for (size_t i = 0; i < out.size(); ++i)
        QVERIFY(std::abs(out[i] - (static_cast<float>((100 + i) % 7) - 3.0f)) < 1e-6f);

    // Reading past the end is clamped; beyond the clip returns false.
    QVERIFY(clip.readFrames(3900, 4096, out));
    QCOMPARE(out.size(), size_t(4096 - 3900));
    QVERIFY(!clip.readFrames(4096, 1, out));
}


void TestClips::streamingClipReadFramesFromFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.path() + "/big.wav";

    // A mono signal where sample i has value f(i), long enough to stream.
    const size_t frames = 48000 * 2;
    std::vector<float> samples(frames);
    for (size_t i = 0; i < frames; ++i)
        samples[i] = ((i % 11) - 5) * 0.1f;
    {
        AudioClip writer(std::move(samples), 48000, 1);
        QVERIFY(writer.saveToFile(path));
    }

    const size_t savedThreshold = AudioClip::streamingThresholdFrames();
    AudioClip::setStreamingThresholdFrames(frames / 2);
    AudioClip clip(path);
    AudioClip::setStreamingThresholdFrames(savedThreshold);

    QVERIFY(clip.isValid());
    QVERIFY(clip.isStreaming());
    QCOMPARE(clip.frameCount(), frames);
    QVERIFY(clip.finePeaks().size() > clip.peaks().size());

    std::vector<float> out;
    QVERIFY(clip.readFrames(48000, 512, out));
    QCOMPARE(out.size(), size_t(512));
    for (size_t i = 0; i < out.size(); ++i) {
        float expected = ((static_cast<size_t>(48000 + i) % 11) - 5) * 0.1f;
        QVERIFY(std::abs(out[i] - expected) < 1e-6f);
    }
}


void TestClips::audioClipFileRoundTrip() {
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


void TestClips::midiClipCloneIndependent() {
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


void TestClips::midiClipNotes() {
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


void TestClips::midiClipSerialization() {
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


QTEST_MAIN(TestClips)
#include "test_model_clips.moc"
