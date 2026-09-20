#include <QTest>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstring>
#include <memory>
#include <vector>
#include <sndfile.h>

#include "core/UndoStack.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "commands/ImportCommands.h"

namespace {

// Write a constant-value mono/stereo WAV at `sampleRate` to `path`.
bool writeTestWav(const QString& path, int sampleRate, int channels, size_t frames) {
    std::vector<float> samples(frames * static_cast<size_t>(channels), 0.4f);
    AudioClip clip(std::move(samples), sampleRate, channels);
    return clip.saveToFile(path);
}

} // namespace

class TestImportCommands : public QObject {
    Q_OBJECT
private slots:
    void prepareImportedClipResamplesToProjectRate();
    void prepareImportedClipRejectsBadInput();
    void importCommandCreatesOneTrackPerFile();
    void importCommandUndoRedo();
    void importMp3File();
};

void TestImportCommands::prepareImportedClipResamplesToProjectRate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Project project;
    project.setFilePath(dir.path() + "/project.json");
    project.setSampleRate(48000);

    const QString src = dir.path() + "/tone.wav";
    QVERIFY(writeTestWav(src, 44100, 2, 4410));

    QString error;
    auto clip = prepareImportedClip(project, src, &error);
    QVERIFY2(clip != nullptr, qPrintable(error));
    QCOMPARE(clip->sampleRate(), project.sampleRate());
    QCOMPARE(clip->channels(), 2);
    QVERIFY(clip->frameCount() > 0);
    // The clip is backed by a WAV inside the project's audio directory.
    QVERIFY(QFileInfo::exists(clip->filePath()));
    QVERIFY(clip->filePath().startsWith(project.audioDirectory()));
}

void TestImportCommands::prepareImportedClipRejectsBadInput() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Project project;
    project.setFilePath(dir.path() + "/project.json");
    project.setSampleRate(48000);

    QString error;
    QVERIFY(prepareImportedClip(project, dir.path() + "/missing.wav", &error) == nullptr);
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(prepareImportedClip(project, QString(), &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TestImportCommands::importCommandCreatesOneTrackPerFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Project project;
    project.setFilePath(dir.path() + "/project.json");
    project.setSampleRate(48000);

    const QString mono = dir.path() + "/mono.wav";
    const QString stereo = dir.path() + "/stereo.wav";
    QVERIFY(writeTestWav(mono, 48000, 1, 512));
    QVERIFY(writeTestWav(stereo, 48000, 2, 256));

    auto monoClip = prepareImportedClip(project, mono);
    auto stereoClip = prepareImportedClip(project, stereo);
    QVERIFY(monoClip && stereoClip);

    std::vector<ImportedAudio> items;
    items.push_back({ QStringLiteral("Alpha"), monoClip->channels(), monoClip });
    items.push_back({ QStringLiteral("Beta"), stereoClip->channels(), stereoClip });

    ImportTracksCommand cmd(project, items);
    cmd.execute();

    QCOMPARE(project.tracks().size(), size_t(2));
    QCOMPARE(project.tracks()[0].name(), QStringLiteral("Alpha"));
    QCOMPARE(project.tracks()[0].channels(), 1);
    QCOMPARE(project.tracks()[1].name(), QStringLiteral("Beta"));
    QCOMPARE(project.tracks()[1].channels(), 2);

    for (size_t i = 0; i < project.tracks().size(); ++i) {
        const Track& track = project.tracks()[i];
        QCOMPARE(track.events().size(), size_t(1));
        const AudioEvent& event = track.events()[0];
        QCOMPARE(event.startSample(), int64_t(0));
        QCOMPARE(event.offsetSample(), int64_t(0));
        const auto& clip = (i == 0) ? monoClip : stereoClip;
        QCOMPARE(event.durationSample(), static_cast<int64_t>(clip->frameCount()));
        QCOMPARE(event.sourceFrames(), static_cast<int64_t>(clip->frameCount()));
    }
}

void TestImportCommands::importCommandUndoRedo() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Project project;
    project.setFilePath(dir.path() + "/project.json");
    project.setSampleRate(48000);

    const QString src = dir.path() + "/one.wav";
    QVERIFY(writeTestWav(src, 48000, 1, 128));
    auto clip = prepareImportedClip(project, src);
    QVERIFY(clip);

    project.addTrack("Existing");

    std::vector<ImportedAudio> items;
    items.push_back({ QStringLiteral("Imported"), clip->channels(), clip });

    UndoStack stack;
    stack.execute(std::make_unique<ImportTracksCommand>(project, items));
    QCOMPARE(project.tracks().size(), size_t(2));
    QCOMPARE(project.tracks().back().name(), QStringLiteral("Imported"));

    stack.undo();
    QCOMPARE(project.tracks().size(), size_t(1));
    QCOMPARE(project.tracks()[0].name(), QStringLiteral("Existing"));

    stack.redo();
    QCOMPARE(project.tracks().size(), size_t(2));
    QCOMPARE(project.tracks().back().events().size(), size_t(1));
}

void TestImportCommands::importMp3File() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.samplerate = 44100;
    info.channels = 2;
    info.format = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    if (!sf_format_check(&info))
        QSKIP("libsndfile was built without MP3 encoding support");

    const QString mp3 = dir.path() + "/song.mp3";
    // Longer than the streaming threshold (~30 s), so the source is a streaming
    // clip and the resampler must read it across several chunks.
    const size_t frames = 1600000;
    std::vector<float> samples(frames * 2, 0.3f);
    SNDFILE* out = sf_open(mp3.toUtf8().constData(), SFM_WRITE, &info);
    QVERIFY(out);
    QCOMPARE(sf_writef_float(out, samples.data(), static_cast<sf_count_t>(frames)),
             static_cast<sf_count_t>(frames));
    sf_close(out);

    Project project;
    project.setFilePath(dir.path() + "/project.json");
    project.setSampleRate(48000);

    QString error;
    auto clip = prepareImportedClip(project, mp3, &error);
    QVERIFY2(clip != nullptr, qPrintable(error));
    QCOMPARE(clip->sampleRate(), project.sampleRate());
    QCOMPARE(clip->channels(), 2);
    // MP3 padding makes the exact length imprecise; it must stay in the ballpark.
    QVERIFY(clip->frameCount() > frames);
    QVERIFY(clip->frameCount() < frames * 2);
}

QTEST_MAIN(TestImportCommands)
#include "test_commands_import.moc"
