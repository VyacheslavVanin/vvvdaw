#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QPixmap>
#include <QSignalSpy>
#include <QTimer>
#include <QContextMenuEvent>
#include <QMimeData>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QFrame>
#include <QDialog>
#include <QWheelEvent>
#include <QScrollBar>
#include <algorithm>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <portaudio.h>

#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "audio/AudioUtils.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioClip.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"
#include "gui/MainWindow.h"
#include "gui/StartDialog.h"
#include "gui/TrackPanelWidget.h"
#include "gui/PanSlider.h"
#include "gui/TrackViewWidget.h"
#include "gui/WaveformPainter.h"
#include "gui/TimelineRuler.h"
#include "gui/MeasureRuler.h"
#include "gui/BusPanelWidget.h"
#include "gui/BusSendsWidget.h"
#include "gui/BusLevelMeter.h"
#include "gui/BusColorBar.h"
#include "gui/InstrumentPanelWidget.h"
#include "gui/PluginListWidget.h"
#include "gui/PluginWindow.h"
#include "gui/PianoRollWindow.h"
#include "gui/PianoRollWidget.h"
#include "gui/ChannelRoutingDialog.h"
#include "gui/SettingsDialog.h"
#include "commands/TrackCommands.h"
#include "commands/SnapshotCommand.h"
#include "GuiTestHelpers.h"

class WaveformTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void trackViewDrawsCrossfadeX();
    void trackViewDrawsCrossfadeAcrossGap();
    void trackViewDrawsRecordingPreview();
    void trackViewDrawsLiveWaveformDuringRecording();
    void trackViewRecordingWaveformAddsDiscretely();
    void waveformShowsIndividualSamplesWhenZoomed();
    void waveformPerSampleLineIsConnected();
    void waveformEnvelopeSymmetricAroundCenter();
    void waveformRendersStreamingClipWhenZoomed();
    void waveformPainterDevicePixelRatio();
private:
    GuiTestEnv m_env;
};

void WaveformTest::initTestCase() {
    if (!m_env.init())
        QSKIP("PortAudio not available");
}

void WaveformTest::cleanupTestCase() {
    m_env.cleanup();
}

void WaveformTest::trackViewDrawsCrossfadeX() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent a, b;
    a.setStartSample(0);
    a.setDurationSample(48000);
    a.setFadeOutSamples(24000); // fade-out tail: [24000, 48000)
    b.setStartSample(48000);
    b.setDurationSample(48000);
    b.setFadeInSamples(24000);  // fade-in head: [48000, 72000)
    track.addEvent(a);
    track.addEvent(b);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001); // 1 px = 1000 samples
    view->setScrollOffset(0);

    // Junction zone spans pixels [24, 72] (fade-out start to fade-in end),
    // vertically the event band [2, 78]. Both diagonals cross the center.
    QImage img = view->grab().toImage();
    QVERIFY(regionHasCrossfade(img, 24, 72, 2, 78));
    QVERIFY(img.pixelColor(48, 40).red() > 240); // center of the X is orange

    // Without opposing fades no crossfade is drawn.
    track.events()[0].setFadeOutSamples(0);
    track.events()[1].setFadeInSamples(0);
    QImage cleared = view->grab().toImage();
    QVERIFY(!regionHasCrossfade(cleared, 24, 72, 2, 78));
}


void WaveformTest::trackViewDrawsCrossfadeAcrossGap() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent a, b;
    a.setStartSample(0);
    a.setDurationSample(48000);
    a.setFadeOutSamples(12000); // tail: [36000, 48000)
    b.setStartSample(60000);    // 12 s gap of silence
    b.setDurationSample(48000);
    b.setFadeInSamples(12000);  // head: [60000, 72000)
    track.addEvent(a);
    track.addEvent(b);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);
    view->setScrollOffset(0);

    // The X still spans the junction across the gap: [36000, 72000) -> [36, 72).
    QImage img = view->grab().toImage();
    QVERIFY(regionHasCrossfade(img, 36, 72, 2, 78));
}


void WaveformTest::trackViewDrawsRecordingPreview() {
    Project project;
    project.addTrack("A1");
    project.tracks()[0].setRecordArmed(true);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002); // 1 sec = 96 px at 48 kHz
    view->setScrollOffset(0);
    view->setPlayheadPosition(48000);

    // Not recording: no recording-colored pixels.
    view->setRecordingPreview(false, 0, -1);
    QImage off = view->grab().toImage();
    QVERIFY(!off.isNull());

    // Recording: the growing rectangle (0..96 px) is drawn with the green
    // recording tint and disappears again when cleared.
    view->setRecordingPreview(true, 0, -1);
    QImage on = view->grab().toImage();
    QVERIFY(!on.isNull());

    QColor cOff = off.pixelColor(48, 40);
    QColor cOn = on.pixelColor(48, 40);
    QVERIFY(cOn != cOff);
    QVERIFY(cOn.green() > cOff.green()); // green tint appears over the grey row

    // Clearing the preview removes the tint again.
    view->setRecordingPreview(false, 0, -1);
    QImage cleared = view->grab().toImage();
    QCOMPARE(cleared.pixelColor(48, 40), cOff);
}


void WaveformTest::trackViewDrawsLiveWaveformDuringRecording() {
    Project project;
    project.addTrack("A1");
    project.tracks()[0].setRecordArmed(true);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002); // 1 sec = 96 px at 48 kHz
    view->setScrollOffset(0);
    view->setPlayheadPosition(48000);
    view->setRecordingPreview(true, 0, -1);

    // No peaks yet: only the green recording tint, no red waveform.
    view->setRecordingPeaks({}, 0, 0);
    QImage empty = view->grab().toImage();
    QVERIFY(!empty.isNull());

    // Two peaks cover 1024 recorded frames -> ~2 px of waveform at the content
    // position (x=0..2). The rest of the rectangle stays empty (no stretching).
    std::vector<AudioClip::Peak> peaks;
    peaks.push_back({-0.9f, 0.9f});
    peaks.push_back({-0.9f, 0.9f});
    view->setRecordingPeaks(peaks, AudioClip::PEAK_STEP_FRAMES,
                            static_cast<int64_t>(peaks.size()) * AudioClip::PEAK_STEP_FRAMES);
    QImage filled = view->grab().toImage();
    QVERIFY(!filled.isNull());

    QColor cWave = filled.pixelColor(1, 40);   // on the 2 px waveform
    QVERIFY(cWave.red() > cWave.green());      // red waveform bar present
    // Beyond the recorded content the area stays exactly as without peaks.
    QCOMPARE(filled.pixelColor(40, 40), empty.pixelColor(40, 40));

    // Removing the peaks clears the waveform again.
    view->setRecordingPeaks({}, 0, 0);
    QImage cleared = view->grab().toImage();
    QCOMPARE(cleared.pixelColor(1, 40), empty.pixelColor(1, 40));
}


void WaveformTest::trackViewRecordingWaveformAddsDiscretely() {
    Project project;
    project.addTrack("A1");
    project.tracks()[0].setRecordArmed(true);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002); // 1 sec = 96 px at 48 kHz
    view->setScrollOffset(0);
    view->setPlayheadPosition(48000); // rectangle spans x=0..96
    view->setRecordingPreview(true, 0, -1);

    // Baseline: nothing recorded yet.
    view->setRecordingPeaks({}, 0, 0);
    QImage baseline = view->grab().toImage();

    // Chunk 1: 5 loud + 5 quiet peaks (5120 frames = ~10 px of waveform).
    std::vector<AudioClip::Peak> p1;
    for (int i = 0; i < 5; ++i) p1.push_back({-0.9f, 0.9f});
    for (int i = 0; i < 5; ++i) p1.push_back({-0.2f, 0.2f});
    view->setRecordingPeaks(p1, AudioClip::PEAK_STEP_FRAMES,
                            static_cast<int64_t>(p1.size()) * AudioClip::PEAK_STEP_FRAMES);
    QImage grab1 = view->grab().toImage();

    // Chunk 2: same first 10 peaks + 10 medium peaks (10240 frames = ~20 px).
    std::vector<AudioClip::Peak> p2 = p1;
    for (int i = 0; i < 10; ++i) p2.push_back({-0.6f, 0.6f});
    view->setRecordingPeaks(p2, AudioClip::PEAK_STEP_FRAMES,
                            static_cast<int64_t>(p2.size()) * AudioClip::PEAK_STEP_FRAMES);
    QImage grab2 = view->grab().toImage();

    // x=15 (frame ~7500) is beyond chunk 1's content (~10 px): it stays empty
    // until chunk 2 lands, then shows the newly appended 0.6 chunk -> discrete
    // addition, not stretching.
    QCOMPARE(grab1.pixelColor(15, 40), baseline.pixelColor(15, 40));
    QVERIFY(grab2.pixelColor(15, 40).red() > grab2.pixelColor(15, 40).green());

    // The far right of the rectangle never fills with waveform: the recorded
    // content is not stretched to cover the growing rectangle.
    QCOMPARE(grab1.pixelColor(60, 40), baseline.pixelColor(60, 40));
    QCOMPARE(grab2.pixelColor(60, 40), baseline.pixelColor(60, 40));

    // The already-drawn region (x=8, frame ~4000, in the quiet section) is
    // pixel-identical: adding a chunk must not re-stretch earlier content.
    QCOMPARE(grab1.pixelColor(8, 40), grab2.pixelColor(8, 40));
}


void WaveformTest::waveformShowsIndividualSamplesWhenZoomed() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];

    std::vector<float> samples;
    for (int i = 0; i < 4096; ++i)
        samples.push_back((i % 2 == 0) ? 0.8f : -0.8f);
    auto clip = std::make_shared<AudioClip>(std::move(samples), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(clip->frameCount());
    ev.setSourceFrames(clip->frameCount());
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::SampleViewPixelsPerSample); // 4 px per sample
    view->setScrollOffset(0);

    QImage img = view->grab().toImage();

    // The per-sample view is a single polyline through the sample values:
    // sample 10 (+0.8) is the top vertex, sample 11 (-0.8) the bottom vertex,
    // and the connecting segment between them crosses the center line. The
    // upper-left corner between the two vertices stays empty (a plain line,
    // not a filled bar).
    QVERIFY(regionHasWaveform(img, 40, 40, 8, 14));   // peak at sample 10
    QVERIFY(regionHasWaveform(img, 44, 44, 65, 75));  // trough at sample 11
    QVERIFY(regionHasWaveform(img, 42, 42, 36, 44));  // connecting segment
    QVERIFY(regionIsBackground(img, 42, 42, 8, 14));
}


void WaveformTest::waveformPerSampleLineIsConnected() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];

    std::vector<float> samples;
    for (int i = 0; i < 4; ++i)
        samples.push_back(0.8f);          // flat top across several samples
    for (int i = 4; i < 4096; ++i)
        samples.push_back((i % 2 == 0) ? 0.8f : -0.8f);
    auto clip = std::make_shared<AudioClip>(std::move(samples), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(clip->frameCount());
    ev.setSourceFrames(clip->frameCount());
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::SampleViewPixelsPerSample);
    view->setScrollOffset(0);

    QImage img = view->grab().toImage();

    // Two equal samples (+0.8 at x=4 and x=8) are joined by a horizontal
    // segment: the intermediate column (x=6) is colored near the top, while
    // the area below the flat line stays background.
    QVERIFY(regionHasWaveform(img, 6, 6, 6, 14));   // horizontal connector
    QVERIFY(regionIsBackground(img, 6, 6, 30, 70)); // nothing below the line
}


void WaveformTest::waveformEnvelopeSymmetricAroundCenter() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];

    std::vector<float> samples;
    for (int i = 0; i < 4096; ++i)
        samples.push_back((i % 2 == 0) ? 0.8f : -0.8f);
    auto clip = std::make_shared<AudioClip>(std::move(samples), 48000, 1);
    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(clip->frameCount());
    ev.setSourceFrames(clip->frameCount());
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::DefaultZoom); // overview: whole clip ~4 px wide
    view->setScrollOffset(0);

    QImage img = view->grab().toImage();

    // The min/max envelope of an alternating ±0.8 signal fills both the upper
    // and the lower half of the column around the center line.
    QColor above = img.pixelColor(2, 10);
    QColor below = img.pixelColor(2, 64);
    QVERIFY(above.blue() > 200);
    QVERIFY(below.blue() > 200);
}


void WaveformTest::waveformRendersStreamingClipWhenZoomed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.path() + "/stream.wav";

    const size_t frames = 48000 * 3; // 3 s
    std::vector<float> samples;
    samples.reserve(frames);
    for (size_t i = 0; i < frames; ++i)
        samples.push_back((i % 2 == 0) ? 0.6f : -0.6f);
    {
        AudioClip writer(std::move(samples), 48000, 1);
        QVERIFY(writer.saveToFile(path));
    }

    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    const size_t savedThreshold = AudioClip::streamingThresholdFrames();
    AudioClip::setStreamingThresholdFrames(48000); // the 3 s file must be streamed
    auto clip = std::make_shared<AudioClip>(path);
    AudioClip::setStreamingThresholdFrames(savedThreshold);
    QVERIFY(clip->isValid());
    QVERIFY(clip->isStreaming());

    AudioEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setOffsetSample(0);
    ev.setDurationSample(clip->frameCount());
    ev.setSourceFrames(clip->frameCount());
    track.addEvent(ev);

    window.rebuildTracks();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(vvvdaw::SampleViewPixelsPerSample);
    view->setScrollOffset(0);

    QImage img = view->grab().toImage();

    // At sample view the streaming clip is decoded from disk and drawn as a
    // connected polyline: sample 10 (+0.6) is the top vertex, sample 11
    // (-0.6) the bottom, and the segment between them crosses the center.
    QVERIFY(regionHasWaveform(img, 40, 40, 15, 21));  // peak at sample 10
    QVERIFY(regionHasWaveform(img, 44, 44, 56, 65));  // trough at sample 11
    QVERIFY(regionHasWaveform(img, 42, 42, 36, 44));  // connecting segment
    QVERIFY(regionIsBackground(img, 42, 42, 15, 21));
}


void WaveformTest::waveformPainterDevicePixelRatio() {
    std::vector<float> samples(1024, 0.0f);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (i % 2 == 0) ? 0.5f : -0.5f;

    // A 1x image keeps logical dimensions; a 2x image allocates 2x physical
    // pixels and carries the DPR so the widget blit stays crisp on HiDPI.
    QImage img1 = WaveformPainter::renderSamples(
        samples.data(), samples.size(), 1, 0, samples.size(), 200, 60, 1.0);
    QCOMPARE(img1.width(), 200);
    QCOMPARE(img1.height(), 60);
    QCOMPARE(img1.devicePixelRatio(), 1.0);

    QImage img2 = WaveformPainter::renderSamples(
        samples.data(), samples.size(), 1, 0, samples.size(), 200, 60, 2.0);
    QCOMPARE(img2.width(), 400);
    QCOMPARE(img2.height(), 120);
    QCOMPARE(img2.devicePixelRatio(), 2.0);

    QImage ps = WaveformPainter::renderSamplesPerSample(
        samples.data(), samples.size(), 1, 0, 64, 4.0, 256, 60, 2.0);
    QCOMPARE(ps.width(), 512);
    QCOMPARE(ps.height(), 120);
    QCOMPARE(ps.devicePixelRatio(), 2.0);
}


QTEST_MAIN(WaveformTest)
#include "test_gui_waveform.moc"
