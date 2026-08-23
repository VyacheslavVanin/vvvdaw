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

namespace {
QComboBox* findComboContaining(QWidget* parent, const QString& text) {
    const auto combos = parent->findChildren<QComboBox*>();
    for (QComboBox* cb : combos)
        for (int i = 0; i < cb->count(); ++i)
            if (cb->itemText(i).contains(text))
                return cb;
    return nullptr;
}

// True if any pixel in the rectangle has the waveform color (high blue, as in
// the default #88ccff waveform). Used so the tests do not depend on the exact
// rasterization row of a 1-px polyline.
bool regionHasWaveform(const QImage& img, int x0, int x1, int y0, int y1,
                       int minBlue = 200) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (img.pixelColor(x, y).blue() > minBlue)
                return true;
    return false;
}

// True if no pixel in the rectangle has the waveform color.
bool regionIsBackground(const QImage& img, int x0, int x1, int y0, int y1,
                        int maxBlue = 100) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (img.pixelColor(x, y).blue() > maxBlue)
                return false;
    return true;
}

// Minimal instrument plugin stub with a configurable output channel count,
// used to exercise the channel routing UI without loading a real plugin.
class StubSynth : public PluginInstance {
public:
    int channels = 2;

    int audioOutputChannels() const override { return channels; }
    std::vector<QString> audioOutputNames() const override {
        return { "Kick", "Snare", "HiHat" };
    }

    bool load(const QString&) override { return true; }
    bool activate(double, int) override { return true; }
    bool deactivate() override { return true; }
    bool process(float**, float**, int, int, const MidiBuffer*) override { return true; }
    QString name() const override { return "Stub"; }
    QString vendor() const override { return "Test"; }
    QString pluginId() const override { return "stub"; }
    QString filePath() const override { return "stub"; }
    bool isActive() const override { return true; }
    void setEnabled(bool) override {}
    bool isEnabled() const override { return true; }
    int latencySamples() const override { return 0; }
    std::vector<PluginPortInfo> ports() const override { return {}; }
    void setParameter(int, float) override {}
    float getParameter(int) const override { return 0.0f; }
    bool hasEditor() const override { return false; }
    void* createEditor(void*) override { return nullptr; }
    void destroyEditor() override {}
    void resizeEditor(int, int) override {}
    bool getEditorSize(int&, int&) const override { return false; }
    QJsonObject stateToJson() const override { return {}; }
    void stateFromJson(const QJsonObject&) override {}
};
} // namespace

// Integration tests for MainWindow::setupUi / rebuildTracks. They run on the
// offscreen Qt platform and a real PortAudio initialization so that device
// enumeration inside rebuildTracks works; they assert the widget structure
// and that rebuildTracks() tracks the project's contents.
class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void constructEmptyProject();
    void constructWithTracks();
    void rebuildAfterTrackChanges();
    void rebuildWithBusesAndInstruments();
    void addTrackViaSignal();
    void moveAudioEventBetweenAudioTracks();
    void moveMidiEventBetweenMidiTracks();
    void midiCrossTrackMoveKeepsSiblingEvents();
    void shiftDragCreatesIndependentMidiCopy();
    void shiftDragOnAudioDoesNotDuplicate();
    void trackViewDrawsRecordingPreview();
    void trackViewDrawsLiveWaveformDuringRecording();
    void trackViewRecordingWaveformAddsDiscretely();
    void waveformShowsIndividualSamplesWhenZoomed();
    void waveformPerSampleLineIsConnected();
    void waveformEnvelopeSymmetricAroundCenter();
    void waveformRendersStreamingClipWhenZoomed();
    void waveformPainterDevicePixelRatio();
    void audioTrackOutComboListsBuses();
    void busPanelStripHasCompactControls();
    void busPanelStripsStayFixedWidth();
    void busPanelToggleRevealsCombinedPanel();
    void busPanelPanelStaysOpenAcrossRebuild();
    void busPanelListsHaveHeaderLabelsAndTopAdd();
    void busPanelSelection();
    void busPanelNameEditing();
    void busPanelFolderFoldUnfold();
    void busPanelContextMenuHasPutToFolder();
    void busPanelDropReorderWithinFolder();
    void busPanelDropTrailingKeepsFolder();
    void busPanelDropReorderFoldersInFolder();
    void busPanelDropFolderIntoFolder();
    void busPanelFolderTintNoIndent();
    void busPanelColorBarAssignsAndPropagates();
    void busPanelColorBarCtrlOverridesChildren();
    void busPanelSendAddAndRemove();
    void busPanelSendContextMenuRemovesSend();
    void busVolumeSliderFollowsMeterDbScale();
    void busLevelMeterIsNarrow();
    void panSliderHighlightsDeviationFromCenter();
    void sliderSizesAreIncreasedForUsability();
    void mainWindowRestoresPanelStateFromSettings();
    void mainWindowRestoresSizeFromSettings();
    void panelTogglesAndGripUpdateSettings();
    void pluginListRowsStayCompact();
    void pluginListHasNoRemoveButton();
    void pluginListContextMenuRemovesPlugin();
    void pluginListContextMenuEmptySpaceOffersAddPlugin();
    void pluginListContextMenuRowOffersAddAndRemove();
    void pluginListContextMenuAddOpensPicker();
    void pluginListDragShowsInsertionLine();
    void pluginListDropFollowsInsertionLine();
    void pluginWindowStaysOnTop();
    void pianoRollWindowStaysOnTop();
    void instrumentOutComboShowsMultiChannel();
    void channelRoutingDialogCreatesBuses();
    void busRenameRefreshesTrackOutCombo();
    void instrumentRenameRefreshesMidiTrackOutCombo();
    void busRenameRefreshesBusAndInstrumentOutCombos();
    void rejectAudioEventToMidiTrack();
    void rejectMidiEventToAudioTrack();
    void startDialogListsRecentProjects();
    void startDialogListsTemplates();
    void startDialogSelectingTemplateSetsChoice();
    void mainWindowFileMenuHasSaveAsTemplate();
    void replaceProjectSwapsAndRebuilds();
    void midiTrackShowsArmButton();
    void settingsDialogHasMidiInputControls();
    void settingsDialogLearnFlow();
    void previewTargetFollowsFocusedPianoRoll();
    void middleDragPansTrackView();
    void ctrlWheelZoomAnchorsCursorFrame();
    void trackViewMouseCursorTracksAndClears();
    void trackViewContextMenuCutSplitsEvent();
    void pianoRollMiddleDragPans();
    void pianoRollCtrlWheelZoomAnchorsCursor();
private:
    QTemporaryDir* m_tmpDir = nullptr;
};

void MainWindowTest::initTestCase() {
    if (Pa_Initialize() != paNoError)
        QSKIP("PortAudio not available");
    m_tmpDir = new QTemporaryDir;
    QVERIFY(m_tmpDir->isValid());
    TemplateStore::setTemplatesDirOverride(m_tmpDir->path());
    TemplateStore::ensureBuiltInTemplates();
}

void MainWindowTest::cleanupTestCase() {
    TemplateStore::setTemplatesDirOverride("");
    delete m_tmpDir;
    m_tmpDir = nullptr;
    Pa_Terminate();
}

void MainWindowTest::constructEmptyProject() {
    Project project;
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.windowTitle().contains(project.name()));
    QVERIFY(window.m_trackRows.empty());
    QVERIFY(window.findChildren<TrackPanelWidget*>().isEmpty());
    QVERIFY(window.findChild<TimelineRuler*>());
    QVERIFY(window.findChild<MeasureRuler*>());
    QVERIFY(window.findChild<BusPanelWidget*>());
    QVERIFY(window.findChild<InstrumentPanelWidget*>());
}

void MainWindowTest::constructWithTracks() {
    Project project;
    project.addTrack("Audio 1");
    project.addTrack("Audio 2", 1);
    project.addMidiTrack("Midi 1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QCOMPARE(window.m_trackRows.size(), size_t(3));
    QCOMPARE(window.findChildren<TrackPanelWidget*>().size(), 3);
    QCOMPARE(window.findChildren<TrackViewWidget*>().size(), 3);
}

void MainWindowTest::rebuildAfterTrackChanges() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    project.addTrack("T2");
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    project.removeTrack(0);
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    project.addMidiTrack("Midi");
    window.rebuildTracks();
    QCOMPARE(window.m_trackRows.size(), size_t(2));
}

void MainWindowTest::rebuildWithBusesAndInstruments() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    AudioBus bus;
    bus.setName("FX");
    project.addBus(std::move(bus));
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    window.rebuildTracks();
    // Buses and instruments do not create track rows.
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    QVERIFY(window.findChild<BusPanelWidget*>());
    QVERIFY(window.findChild<InstrumentPanelWidget*>());
}

void MainWindowTest::addTrackViaSignal() {
    Project project;
    project.addTrack("T1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    auto panels = window.findChildren<TrackPanelWidget*>();
    QCOMPARE(panels.size(), 1);
    emit panels[0]->addTrackRequested(2);

    // The command pipeline rebuilds the rows synchronously; flush any
    // deferred widget deletions from the rebuild before counting.
    QCoreApplication::processEvents();
    QCOMPARE(window.m_trackRows.size(), size_t(2));
    QCOMPARE(window.m_project.tracks().size(), size_t(2));
}

void MainWindowTest::moveAudioEventBetweenAudioTracks() {
    Project project;
    project.addTrack("A1");
    project.addTrack("A2");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    AudioEvent ev;
    ev.setStartSample(100);
    src.addEvent(ev);
    const int64_t id = src.events().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.moveEventToTrack(0, 1, id, 500));
    QVERIFY(src.events().empty());
    QCOMPARE(dst.events().size(), size_t(1));
    QCOMPARE(dst.events().front().id(), id);
    QCOMPARE(dst.events().front().startSample(), int64_t(500));
}

void MainWindowTest::moveMidiEventBetweenMidiTracks() {
    Project project;
    project.addMidiTrack("M1");
    project.addMidiTrack("M2");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    MidiEvent ev;
    ev.setStartSample(100);
    src.addMidiEvent(ev);
    const int64_t id = src.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.moveEventToTrack(0, 1, id, 500));
    QVERIFY(src.midiEvents().empty());
    QCOMPARE(dst.midiEvents().size(), size_t(1));
    QCOMPARE(dst.midiEvents().front().id(), id);
    QCOMPARE(dst.midiEvents().front().startSample(), int64_t(500));
}

void MainWindowTest::midiCrossTrackMoveKeepsSiblingEvents() {
    Project project;
    project.addMidiTrack("M1");
    project.addMidiTrack("M2");
    Track& a = project.tracks()[0];
    Track& b = project.tracks()[1];

    MidiEvent ea;
    ea.setStartSample(0);
    a.addMidiEvent(ea);
    MidiEvent eb;
    eb.setStartSample(100);
    a.addMidiEvent(eb);
    MidiEvent ec;
    ec.setStartSample(0);
    b.addMidiEvent(ec);
    QCOMPARE(a.midiEvents().size(), size_t(2));
    QCOMPARE(b.midiEvents().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    // A's first event (id 1) collides with B's own first event (id 1) when moved in.
    const int64_t aFirstId = a.midiEvents()[0].id();
    QVERIFY(window.moveEventToTrack(0, 1, aFirstId, 500));
    QCOMPARE(b.midiEvents().size(), size_t(2));
    QVERIFY(b.midiEvents()[0].id() != b.midiEvents()[1].id());

    // Moving one of B's two events out must not take the sibling along.
    const int64_t moveOutId = b.midiEvents()[0].id();
    QVERIFY(window.moveEventToTrack(1, 0, moveOutId, 600));
    QCOMPARE(b.midiEvents().size(), size_t(1)); // sibling survives
    QCOMPARE(a.midiEvents().size(), size_t(2));
}

void MainWindowTest::shiftDragCreatesIndependentMidiCopy() {
    Project project;
    project.addMidiTrack("M1");
    Track& track = project.tracks()[0];
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);
    QCOMPARE(track.midiEvents().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002); // event spans 96 px, click well inside it
    QVERIFY(view->isVisible());

    QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
    QCOMPARE(track.midiEvents().size(), size_t(2));
    QVERIFY(track.midiEvents()[0].clip() != track.midiEvents()[1].clip());
    QCOMPARE(track.midiEvents()[1].clip()->notes().size(), size_t(1));

    // Editing the copy's clip must not affect the original event's clip.
    track.midiEvents()[1].clip()->addNote(72, 120, 240, 240);
    QCOMPARE(track.midiEvents()[1].clip()->notes().size(), size_t(2));
    QCOMPARE(track.midiEvents()[0].clip()->notes().size(), size_t(1));

    QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
}

void MainWindowTest::shiftDragOnAudioDoesNotDuplicate() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);
    QCOMPARE(track.events().size(), size_t(1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);
    QVERIFY(view->isVisible());

    // Shift on an audio event is a plain move, not a duplicate.
    QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
    QCOMPARE(track.events().size(), size_t(1));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(40, 40));
}

void MainWindowTest::trackViewDrawsRecordingPreview() {
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

void MainWindowTest::trackViewDrawsLiveWaveformDuringRecording() {
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

void MainWindowTest::trackViewRecordingWaveformAddsDiscretely() {
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

void MainWindowTest::waveformShowsIndividualSamplesWhenZoomed() {
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

void MainWindowTest::waveformPerSampleLineIsConnected() {
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

void MainWindowTest::waveformEnvelopeSymmetricAroundCenter() {
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

void MainWindowTest::waveformRendersStreamingClipWhenZoomed() {
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

void MainWindowTest::waveformPainterDevicePixelRatio() {
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

void MainWindowTest::audioTrackOutComboListsBuses() {
    Project project;
    project.addTrack("Audio 1");
    project.addMidiTrack("Midi 1");
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(2));

    QComboBox* audioOut = findComboContaining(window.m_trackRows[0].panel, "Master");
    QVERIFY(audioOut);
    QCOMPARE(audioOut->count(), static_cast<int>(project.buses().size()));
    for (int i = 0; i < audioOut->count(); ++i) {
        QVERIFY(!audioOut->itemText(i).contains("Inst:"));
        QVERIFY(!audioOut->itemText(i).contains("MIDI:"));
    }

    QComboBox* midiOut = findComboContaining(window.m_trackRows[1].panel, "Inst:");
    QVERIFY(midiOut);
    bool foundInst = false;
    for (int i = 0; i < midiOut->count(); ++i) {
        if (midiOut->itemText(i).contains("Inst: Pad"))
            foundInst = true;
        // The MIDI out combo must not list any project bus as an item (checked
        // by exact name: a MIDI output device may legitimately contain "Master"
        // as a substring in its own name).
        for (const auto& bus : project.buses())
            QVERIFY(midiOut->itemText(i) != bus.name());
    }
    QVERIFY(foundInst);
}

void MainWindowTest::instrumentOutComboShowsMultiChannel() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    inst.setOutputBusIndex(1);
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_instrumentPanel->rebuild();

    auto* panel = window.m_instrumentPanel;

    // The out combo offers a "Multi Channel" entry; single-bus instruments
    // keep the plain bus selection.
    QComboBox* out = nullptr;
    for (QComboBox* cb : panel->findChildren<QComboBox*>()) {
        bool hasMulti = false;
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toInt() == -1 && cb->itemText(i) == "Multi Channel")
                hasMulti = true;
        }
        if (hasMulti) { out = cb; break; }
    }
    QVERIFY(out);
    QCOMPARE(out->currentData().toInt(), 1); // routed to bus 1, not multi

    // An instrument in multi-channel mode selects the "Multi Channel" entry.
    std::vector<Instrument::ChannelRoute> routes;
    Instrument::ChannelRoute r;
    r.busIndex = 0;
    r.name = "Ch0";
    routes.push_back(r);
    project.instruments()[0].setMultiChannel(true);
    project.instruments()[0].setChannelRoutes(routes);
    window.m_instrumentPanel->rebuild();

    QComboBox* multiOut = nullptr;
    for (QComboBox* cb : panel->findChildren<QComboBox*>()) {
        if (cb->currentData().toInt() == -1) {
            multiOut = cb;
            break;
        }
    }
    QVERIFY(multiOut);
    QCOMPARE(multiOut->currentText(), QString("Multi Channel"));
}

void MainWindowTest::channelRoutingDialogCreatesBuses() {
    Project project;
    Instrument inst;
    inst.setName("Drums");
    auto synth = std::make_unique<StubSynth>();
    synth->channels = 3;
    inst.setSynth(std::move(synth));
    project.addInstrument(std::move(inst));

    const int busCountBefore = static_cast<int>(project.buses().size()); // 2

    ChannelRoutingDialog dialog(project, project.instruments()[0],
                                project.instruments()[0].synth());

    auto* createBtn = dialog.findChild<QPushButton*>("createBusesButton");
    QVERIFY(createBtn);
    createBtn->click();

    // One new bus per channel, each channel assigned to its own bus.
    QCOMPARE(project.buses().size(), size_t(busCountBefore + 3));
    QCOMPARE(dialog.createdBusCount(), 3);

    auto* table = dialog.findChild<QTableWidget*>();
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 3);
    for (int c = 0; c < 3; ++c) {
        auto* combo = qobject_cast<QComboBox*>(table->cellWidget(c, 1));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), busCountBefore + c);
    }

    // Buses are named after the (default) channel names.
    QCOMPARE(project.buses()[busCountBefore].name(), QString("Kick"));
    QCOMPARE(project.buses()[busCountBefore + 1].name(), QString("Snare"));
    QCOMPARE(project.buses()[busCountBefore + 2].name(), QString("HiHat"));

    // Rejecting the dialog rolls the created buses back out of the project.
    dialog.reject();
    QCOMPARE(project.buses().size(), size_t(busCountBefore));
    QCOMPARE(dialog.createdBusCount(), 0);
}

void MainWindowTest::busPanelStripHasCompactControls() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1)); // Master, Metronome, FX

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    // One vertical volume slider, level meter and plugin toggle per bus.
    const auto volumeSliders = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(volumeSliders.size(), 3);
    for (QSlider* s : volumeSliders)
        QCOMPARE(s->orientation(), Qt::Vertical);

    const auto meters = window.m_busPanel->findChildren<BusLevelMeter*>("levelMeter");
    QCOMPARE(meters.size(), 3);

    const auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QCOMPARE(toggles.size(), 3);
    for (QPushButton* b : toggles)
        QVERIFY(b->isCheckable());

    // S/M buttons live below the name (one pair per bus).
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("soloButton").size(), 3);
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("muteButton").size(), 3);

    // The collapsed strip is much narrower than before.
    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    QVERIFY(strip->sizeHint().width() <= 100);

    // Plugin and send lists exist but are hidden until the panel is toggled.
    const auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(lists.size(), 3);
    for (QWidget* l : lists)
        QVERIFY(!l->isVisible());
    const auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    for (QWidget* l : sendLists)
        QVERIFY(!l->isVisible());
}

void MainWindowTest::busPanelStripsStayFixedWidth() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();

    auto stripWidths = [&window]() {
        std::vector<int> widths;
        const auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
        for (QPushButton* t : toggles)
            widths.push_back(t->parentWidget()->parentWidget()->width());
        return widths;
    };

    window.resize(500, 400);
    window.show();
    QCoreApplication::processEvents();
    const auto narrow = stripWidths();
    QCOMPARE(narrow.size(), 3);

    window.resize(1200, 400);
    QCoreApplication::processEvents();
    const auto wide = stripWidths();
    QCOMPARE(wide.size(), 3);

    // Strips must not stretch when the window widens.
    for (size_t i = 0; i < narrow.size(); ++i)
        QCOMPARE(wide[i], narrow[i]);
}

void MainWindowTest::busPanelToggleRevealsCombinedPanel() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    auto panels = window.m_busPanel->findChildren<QWidget*>("busFxPanel");
    auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(panels.size(), 3);
    QCOMPARE(lists.size(), 3);
    QCOMPARE(sendLists.size(), 3);

    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    const int collapsedWidth = strip->sizeHint().width();

    // The combined panel is hidden and hides both the plugin and send lists.
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!sendLists[0]->isVisible());
    toggles[0]->click();
    QVERIFY(!panels[0]->isHidden());
    QVERIFY(lists[0]->isVisible());
    QVERIFY(sendLists[0]->isVisible());
    QVERIFY(strip->sizeHint().width() > collapsedWidth); // strip widens

    // Plugins are stacked above the sends.
    QVERIFY(lists[0]->mapTo(panels[0], QPoint(0, 0)).y()
            < sendLists[0]->mapTo(panels[0], QPoint(0, 0)).y());

    toggles[0]->click();
    QVERIFY(panels[0]->isHidden());
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!sendLists[0]->isVisible());
}

void MainWindowTest::busPanelPanelStaysOpenAcrossRebuild() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(lists.size(), 3);
    QVERIFY(!lists[2]->isVisible());
    toggles[2]->click();
    QVERIFY(lists[2]->isVisible());

    // A full panel rebuild (e.g. triggered after adding a plugin or send) must
    // not collapse the explicitly opened combined panel.
    window.m_busPanel->rebuild();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(lists.size(), 3);
    QCOMPARE(sendLists.size(), 3);
    QVERIFY(toggles[2]->isChecked());
    QVERIFY(lists[2]->isVisible());
    QVERIFY(sendLists[2]->isVisible());

    // The other (never opened) panels stay collapsed.
    QVERIFY(!toggles[0]->isChecked());
    QVERIFY(!lists[0]->isVisible());
    QVERIFY(!toggles[1]->isChecked());
    QVERIFY(!lists[1]->isVisible());
}

void MainWindowTest::busPanelListsHaveHeaderLabelsAndTopAdd() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QCoreApplication::processEvents();

    auto pluginLists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(pluginLists.size(), 3);
    QCOMPARE(sendLists.size(), 3);
    PluginListWidget* plist = pluginLists[2];
    BusSendsWidget* slist = sendLists[2];

    // Header captions next to the add buttons.
    QLabel* effectsLabel = nullptr;
    QLabel* sendsLabel = nullptr;
    for (QLabel* lb : plist->findChildren<QLabel*>())
        if (lb->text().contains("Effects") && lb->isVisible()) { effectsLabel = lb; break; }
    for (QLabel* lb : slist->findChildren<QLabel*>())
        if (lb->text().contains("Sends") && lb->isVisible()) { sendsLabel = lb; break; }
    QVERIFY(effectsLabel);
    QVERIFY(sendsLabel);

    // The "+" buttons sit in the top half, on the same row as their label.
    QPushButton* plistAdd = nullptr;
    for (QPushButton* b : plist->findChildren<QPushButton*>())
        if (b->text() == "+") { plistAdd = b; break; }
    QPushButton* sendAdd = slist->findChild<QPushButton*>("sendAddButton");
    QVERIFY(plistAdd);
    QVERIFY(sendAdd);
    QVERIFY(plistAdd->y() < plist->height() / 2);
    QVERIFY(sendAdd->y() < slist->height() / 2);
    QCOMPARE(plistAdd->y(), effectsLabel->y());
    QCOMPARE(sendAdd->y(), sendsLabel->y());
}

// Send a left-button press to a widget (used to drive strip selection).
static void pressLeft(QWidget* w, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), w->mapToGlobal(QPoint(5, 5)),
                      Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(w, &press);
}

void MainWindowTest::busPanelSelection() {
    Project project;
    AudioBus b1;
    b1.setName("B1");
    project.addBus(std::move(b1)); // index 2
    AudioBus b2;
    b2.setName("B2");
    project.addBus(std::move(b2)); // index 3

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QCOMPARE(toggles.size(), 4); // Master, Metronome, B1, B2
    QWidget* c2 = toggles[2]->parentWidget(); // B1 controls
    QWidget* c3 = toggles[3]->parentWidget(); // B2 controls

    // Plain click selects a single bus.
    pressLeft(c2);
    auto sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(1));
    QCOMPARE(sel[0], 2);

    // Ctrl-click adds another bus to the selection.
    pressLeft(c3, Qt::ControlModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(2));
    QVERIFY(std::find(sel.begin(), sel.end(), 2) != sel.end());
    QVERIFY(std::find(sel.begin(), sel.end(), 3) != sel.end());

    // Ctrl-click again removes it.
    pressLeft(c3, Qt::ControlModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(1));
    QCOMPARE(sel[0], 2);

    // Re-anchor on bus 2, then shift-click selects the range 2..3.
    pressLeft(c2);
    pressLeft(c3, Qt::ShiftModifier);
    sel = window.m_busPanel->selectedBusIndices();
    QCOMPARE(sel.size(), size_t(2));
    QVERIFY(std::find(sel.begin(), sel.end(), 2) != sel.end());
    QVERIFY(std::find(sel.begin(), sel.end(), 3) != sel.end());
}

void MainWindowTest::busPanelNameEditing() {
    Project project;
    AudioBus b1;
    b1.setName("B1");
    project.addBus(std::move(b1)); // index 2

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto edits = window.m_busPanel->findChildren<QLineEdit*>();
    QVERIFY(edits.size() >= 3);
    QLineEdit* name = edits[2]; // B1
    QVERIFY(name->isReadOnly());

    // Double-click makes the name editable (cursor appears).
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(5, 5), QPointF(5, 5),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(name, &dbl);
    QVERIFY(!name->isReadOnly());
}

void MainWindowTest::busPanelFolderFoldUnfold() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3
    project.buses()[3].setOutputBusIndex(2); // Child routes into Folder

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    // Folder is unfolded by default: all four strips are built.
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 4);
    auto folderToggles = window.m_busPanel->findChildren<QPushButton*>("folderToggle");
    QCOMPARE(folderToggles.size(), 1); // only the folder bus

    // Collapse the folder: the child strip disappears.
    folderToggles[0]->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 3);

    // Unfold again: the child returns.
    folderToggles = window.m_busPanel->findChildren<QPushButton*>("folderToggle");
    QCOMPARE(folderToggles.size(), 1);
    folderToggles[0]->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(window.m_busPanel->findChildren<QWidget*>("busStrip").size(), 4);
}

void MainWindowTest::busPanelContextMenuHasPutToFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    QWidget* c3 = toggles[3]->parentWidget(); // Child controls

    // Open the context menu and inspect its actions.
    bool sawPutToFolder = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                for (QAction* a : m->actions())
                    if (a->text().contains("Put to folder"))
                        sawPutToFolder = true;
                m->close();
            }
        }
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, c3->rect().center(),
                         c3->mapToGlobal(c3->rect().center()));
    QApplication::sendEvent(c3, &ev);
    QCoreApplication::processEvents();
    QVERIFY(sawPutToFolder);
}

void MainWindowTest::busPanelDropReorderWithinFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus a;
    a.setName("A");
    project.addBus(std::move(a)); // index 3
    AudioBus b;
    b.setName("B");
    project.addBus(std::move(b)); // index 4
    project.buses()[3].setOutputBusIndex(2); // A -> Folder
    project.buses()[4].setOutputBusIndex(2); // B -> Folder
    project.setBusDisplayOrder({ 0, 1, 2, 4, 3 }); // B renders before A

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 5); // Master, Metronome, Folder, B, A

    // Drop B (4) on A's (3) right half: B is inserted after A inside the folder.
    QWidget* aStrip = strips[4]; // last in render order
    QPoint dropPos = aStrip->geometry().center();
    dropPos.rx() += aStrip->width() / 4;

    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    // B stayed in the folder and now sorts after A.
    QCOMPARE(project.buses()[4].outputBusIndex(), 2);
    const auto& order = project.busDisplayOrder();
    auto itA = std::find(order.begin(), order.end(), 3);
    auto itB = std::find(order.begin(), order.end(), 4);
    QVERIFY(itA != order.end() && itB != order.end());
    QVERIFY(itA < itB);
}

void MainWindowTest::busPanelDropTrailingKeepsFolder() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2
    AudioBus a;
    a.setName("A");
    project.addBus(std::move(a)); // index 3
    AudioBus b;
    b.setName("B");
    project.addBus(std::move(b)); // index 4
    project.buses()[3].setOutputBusIndex(2); // A -> Folder
    project.buses()[4].setOutputBusIndex(2); // B -> Folder

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    QWidget* container = window.m_busPanel->findChild<QWidget*>("busContainer");
    QVERIFY(container);

    // Drop B on the empty space beyond all strips: it must NOT be ejected.
    QPoint dropPos(container->width() - 5, 10);
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 2); // still in the folder
}

void MainWindowTest::busPanelDropReorderFoldersInFolder() {
    Project project;
    AudioBus o;
    o.setName("O");
    project.addBus(std::move(o)); // index 2
    AudioBus f1;
    f1.setName("F1");
    project.addBus(std::move(f1)); // index 3
    AudioBus f2;
    f2.setName("F2");
    project.addBus(std::move(f2)); // index 4
    AudioBus c1;
    c1.setName("c1");
    project.addBus(std::move(c1)); // index 5
    project.buses()[3].setOutputBusIndex(2); // F1 -> O
    project.buses()[4].setOutputBusIndex(2); // F2 -> O
    project.buses()[5].setOutputBusIndex(3); // c1 -> F1

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 6); // Master, Metro, O, F1, c1, F2
    QWidget* f1Strip = strips[3];
    QWidget* f2Strip = strips[5];

    // Drop F2 on F1's left quarter: F2 becomes F1's sibling (still in O).
    QPoint dropPos(f1Strip->geometry().left() + f1Strip->width() / 8,
                   f1Strip->geometry().center().y());
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 2); // still in O, not inside F1
    const auto& order = project.busDisplayOrder();
    auto itF1 = std::find(order.begin(), order.end(), 3);
    auto itF2 = std::find(order.begin(), order.end(), 4);
    QVERIFY(itF1 != order.end() && itF2 != order.end());
    QVERIFY(itF2 < itF1); // F2 is now before F1
}

void MainWindowTest::busPanelDropFolderIntoFolder() {
    Project project;
    AudioBus o;
    o.setName("O");
    project.addBus(std::move(o)); // index 2
    AudioBus f1;
    f1.setName("F1");
    project.addBus(std::move(f1)); // index 3
    AudioBus f2;
    f2.setName("F2");
    project.addBus(std::move(f2)); // index 4
    AudioBus c1;
    c1.setName("c1");
    project.addBus(std::move(c1)); // index 5
    project.buses()[3].setOutputBusIndex(2); // F1 -> O
    project.buses()[4].setOutputBusIndex(2); // F2 -> O
    project.buses()[5].setOutputBusIndex(3); // c1 -> F1

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 6);
    QWidget* f1Strip = strips[3];

    // Drop F2 on F1's body: F2 moves into F1 (nesting is still possible).
    QPoint dropPos = f1Strip->geometry().center();
    window.m_busPanel->handleBusDrop(dropPos, { 4 });
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[4].outputBusIndex(), 3); // F2 is now inside F1
}

void MainWindowTest::busPanelFolderTintNoIndent() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F)
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    AudioBus nested;
    nested.setName("Nested");
    project.addBus(std::move(nested)); // index 4 (N) -> F
    AudioBus grandchild;
    grandchild.setName("Grandchild");
    project.addBus(std::move(grandchild)); // index 5 (G) -> N
    AudioBus top;
    top.setName("Top");
    project.addBus(std::move(top)); // index 6 (T) -> master
    project.buses()[3].setOutputBusIndex(2); // C -> F
    project.buses()[4].setOutputBusIndex(2); // N -> F
    project.buses()[5].setOutputBusIndex(4); // G -> N
    project.buses()[6].setOutputBusIndex(0); // T -> master

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    window.m_busPanelGrip->show();
    QCoreApplication::processEvents();

    // Render order: Master(0), Metronome(1), Folder(2), Child(3), Nested(4),
    // Grandchild(5), Top(6).
    auto strips = window.m_busPanel->findChildren<QWidget*>("busStrip");
    QCOMPARE(strips.size(), 7);
    QWidget* folderStrip = strips[2];
    QWidget* childStrip = strips[3];
    QWidget* nestedStrip = strips[4];
    QWidget* grandchildStrip = strips[5];
    QWidget* topStrip = strips[6];

    // No indentation: every strip sits edge to edge with only the container's
    // plain spacing (the old code inserted 14px/28px indent spacers per level,
    // so folder/child gaps were far wider than the top-level gap).
    int plainGap = strips[1]->geometry().left() - strips[0]->geometry().right();
    QVERIFY(plainGap < 10); // a sane plain layout spacing
    for (int i = 1; i + 1 < strips.size(); ++i)
        QCOMPARE(strips[i + 1]->geometry().left() - strips[i]->geometry().right(),
                 plainGap);

    // Folder membership via a shared tint: the folder and its direct children
    // share one tone, a nested folder forms its own distinct group.
    auto stripColor = [](QWidget* w) { return w->palette().color(QPalette::Window); };
    QVERIFY(project.isBusFolder(2));
    QCOMPARE(stripColor(childStrip), stripColor(folderStrip));      // C shares F's tint
    QCOMPARE(stripColor(grandchildStrip), stripColor(nestedStrip)); // G shares N's tint
    QVERIFY(stripColor(nestedStrip) != stripColor(folderStrip));    // nested folder: own hue
    QVERIFY(stripColor(grandchildStrip) != stripColor(childStrip)); // nested group distinct

    // Top-level buses keep the plain alternating tone (no tint).
    QCOMPARE(stripColor(topStrip), QColor("#2e2e2e"));
    QCOMPARE(stripColor(strips[1]), QColor("#333333")); // Metronome
}

void MainWindowTest::busPanelColorBarAssignsAndPropagates() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F) -> master
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    project.buses()[3].setOutputBusIndex(2);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    QCoreApplication::processEvents();

    // Render order: Master(0), Metronome(1), Folder(2), Child(3).
    auto bars = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(bars.size(), 4);
    for (BusColorBar* b : bars)
        QVERIFY(b->height() >= 4 && b->height() <= 6); // ~5px tall strip

    // Before any manual color the bars show the automatic/effective color.
    QCOMPARE(bars[2]->color(), project.busColor(2));
    QCOMPARE(bars[3]->color(), project.busColor(3));

    // Stub out the modal picker and assign a color to the folder.
    bars[2]->setColorPickerForTesting([](const QColor&) { return QColor("#ff0000"); });
    QSignalSpy spy(window.m_busPanel, &BusPanelWidget::busColorWillChange);
    bars[2]->pickColor();

    QVERIFY(project.buses()[2].colorSet());
    QCOMPARE(project.buses()[2].color(), QColor("#ff0000"));
    // The child keeps no manual color but inherits the folder's color.
    QVERIFY(!project.buses()[3].colorSet());
    QCOMPARE(project.busColor(3), QColor("#ff0000"));
    QCOMPARE(spy.count(), 1);

    // The panel refreshed the bars to the new color. The assignment may have
    // triggered a rebuild that scheduled the old bars for deletion; flush them
    // before re-fetching.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto refreshed = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(refreshed[2]->color(), QColor("#ff0000"));

    // Undo restores the automatic color.
    window.performUndo();
    QVERIFY(!project.buses()[2].colorSet());
}

void MainWindowTest::busPanelColorBarCtrlOverridesChildren() {
    Project project;
    AudioBus folder;
    folder.setName("Folder");
    project.addBus(std::move(folder)); // index 2 (F)
    AudioBus child;
    child.setName("Child");
    project.addBus(std::move(child)); // index 3 (C) -> F
    AudioBus grandchild;
    grandchild.setName("Grand");
    project.addBus(std::move(grandchild)); // index 4 (G) -> C
    project.buses()[3].setOutputBusIndex(2);
    project.buses()[4].setOutputBusIndex(3);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    window.m_busPanel->show();
    QCoreApplication::processEvents();

    auto bars = window.m_busPanel->findChildren<BusColorBar*>("busColorBar");
    QCOMPARE(bars.size(), 5);
    // Folder is index 2 -> bars[2].
    bars[2]->setColorPickerForTesting([](const QColor&) { return QColor("#00ff00"); });

    // Give the child a manual color first, so we can verify Ctrl clears it.
    project.buses()[3].setColor(QColor("#101010"));
    QVERIFY(project.buses()[3].colorSet());

    // Ctrl+click assigns the folder's color and clears the manual-color flag on
    // all descendants, so they inherit the folder's color.
    QTest::mouseClick(bars[2], Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(project.buses()[2].color(), QColor("#00ff00"));
    QVERIFY(project.buses()[2].colorSet());
    QVERIFY(!project.buses()[3].colorSet()); // flag cleared
    QVERIFY(!project.buses()[4].colorSet());
    QCOMPARE(project.busColor(3), QColor("#00ff00")); // inherits the folder
    QCOMPARE(project.busColor(4), QColor("#00ff00")); // inherits recursively

    // A single undo reverts the whole override, restoring the child's manual
    // color.
    window.performUndo();
    QVERIFY(!project.buses()[2].colorSet());
    QVERIFY(project.buses()[3].colorSet());              // child's manual color back
    QCOMPARE(project.buses()[3].color(), QColor("#101010"));
    QVERIFY(!project.buses()[4].colorSet());
}

void MainWindowTest::busPanelSendAddAndRemove() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QVERIFY(project.buses()[2].sends().empty());

    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    auto* addBtn = sendLists[2]->findChild<QPushButton*>("sendAddButton");
    QVERIFY(addBtn);
    addBtn->click();
    QCOMPARE(project.buses()[2].sends().size(), size_t(1));
    QCOMPARE(project.buses()[2].sends()[0].busIndex, 0);
    QCOMPARE(project.buses()[2].sends()[0].preFader, false);

    // The add rebuilt the panel; flush deferred deletes and re-fetch.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    BusSendsWidget* sendList = sendLists[2];

    const auto combos = sendList->findChildren<QComboBox*>("sendTargetCombo");
    QCOMPARE(combos.size(), 1);
    const auto levelSliders = sendList->findChildren<QSlider*>("sendLevelSlider");
    QCOMPARE(levelSliders.size(), 1);
    const auto preToggles = sendList->findChildren<QPushButton*>("sendPreToggle");
    QCOMPARE(preToggles.size(), 1);

    // Changing the level slider (dB scale) updates the model.
    levelSliders[0]->setValue(50); // -30 dB
    QVERIFY(std::abs(project.buses()[2].sends()[0].level - decibelsToLinear(-30.0f)) < 1e-3f);

    // The Pre/Post toggle flips the pre-fader flag.
    preToggles[0]->click();
    QCOMPARE(project.buses()[2].sends()[0].preFader, true);

    // Undoing the level, pre and add commands restores the empty list.
    while (project.buses()[2].sends().size() > 0)
        window.performUndo();
    QCOMPARE(project.buses()[2].sends().size(), size_t(0));
}

void MainWindowTest::busVolumeSliderFollowsMeterDbScale() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    b1.setVolume(0.5f); // -6.02 dB
    project.addBus(std::move(b1)); // Master, Metronome, FX

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    const auto sliders = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(sliders.size(), 3);
    QSlider* slider = sliders[2]; // third row = FX bus
    AudioBus& fx = window.m_project.buses()[2];

    // The slider position is derived from the linear volume through the meter's
    // dB scale: 0.5 (-6.02 dB) sits ~90 of 100.
    QVERIFY(std::abs(slider->value() - 90) <= 1);

    // Full scale = 0 dB = unity.
    slider->setValue(100);
    QCOMPARE(fx.volume(), 1.0f);

    // Bottom of the fader = -60 dB = silence.
    slider->setValue(0);
    QCOMPARE(fx.volume(), 0.0f);

    // Midpoint = -30 dB, matching the same point on the meter scale.
    slider->setValue(50);
    QVERIFY(std::abs(fx.volume() - decibelsToLinear(-30.0f)) < 1e-3f);
}

void MainWindowTest::busPanelSendContextMenuRemovesSend() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));
    AudioBus::Send s;
    s.busIndex = 0;
    project.buses()[2].sends().push_back(s);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("panelToggle");
    toggles[2]->click(); // open the combined panel on the FX bus
    QCoreApplication::processEvents();

    auto sendLists = window.m_busPanel->findChildren<BusSendsWidget*>("busSendList");
    QCOMPARE(sendLists.size(), 3);
    BusSendsWidget* sendList = sendLists[2];
    QCOMPARE(project.buses()[2].sends().size(), size_t(1));

    QWidget* row = sendList->findChild<QWidget*>("sendRow");
    QVERIFY(row);

    // Drive the context menu: open it on the row and trigger "Remove Send".
    QTimer::singleShot(0, [] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                for (QAction* a : m->actions())
                    if (a->text().contains("Remove Send")) { menu = m; break; }
            }
            if (menu) break;
        }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Remove Send")) {
                a->trigger();
                break;
            }
        }
        menu->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, row->rect().center(),
                         row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &ev);
    QCoreApplication::processEvents();

    QCOMPARE(project.buses()[2].sends().size(), size_t(0));
}

void MainWindowTest::busLevelMeterIsNarrow() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    const auto meters = window.m_busPanel->findChildren<BusLevelMeter*>("levelMeter");
    QCOMPARE(meters.size(), 3);
    for (BusLevelMeter* m : meters)
        QVERIFY(m->maximumWidth() < 34); // indicator bar column is narrower

    // Painting the dB scale plus the volume marker must not crash.
    BusLevelMeter* meter = meters[0];
    meter->resize(30, 120);
    meter->setPeak(0.5f);
    meter->setVolume(0.5f);
    meter->setClipping(true);
    QPixmap pm = meter->grab();
    QVERIFY(!pm.isNull());
}

void MainWindowTest::pluginListRowsStayCompact() {
    AudioBus bus;
    for (int i = 0; i < 3; ++i) {
        auto synth = std::make_unique<StubSynth>();
        bus.pluginChain().addPlugin(std::move(synth));
    }

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    // Each plugin row keeps a compact height instead of stretching to fill
    // the whole list (which would give ~66 px per row here).
    int rowCount = 0;
    for (QPushButton* b : list.findChildren<QPushButton*>()) {
        if (b->text() == "ON" || b->text() == "OFF") {
            ++rowCount;
            QVERIFY(b->parentWidget()->height() <= 30);
        }
    }
    QCOMPARE(rowCount, 3);
}

void MainWindowTest::pluginListHasNoRemoveButton() {
    AudioBus bus;
    auto synth = std::make_unique<StubSynth>();
    bus.pluginChain().addPlugin(std::move(synth));

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();

    for (QPushButton* b : list.findChildren<QPushButton*>())
        QVERIFY(b->text() != "x"); // deletion is done via the context menu
}

void MainWindowTest::pluginListContextMenuRemovesPlugin() {
    AudioBus bus;
    auto synth = std::make_unique<StubSynth>();
    bus.pluginChain().addPlugin(std::move(synth));

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 120);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QCOMPARE(bus.pluginChain().count(), 1);

    // Drive the context menu: open it on the row and trigger "Remove Plugin".
    QTimer::singleShot(0, [] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Remove")) {
                a->trigger();
                break;
            }
        }
        menu->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, row->rect().center(),
                         row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &ev);
    QCoreApplication::processEvents();

    QCOMPARE(bus.pluginChain().count(), 0);
}

void MainWindowTest::pluginListContextMenuEmptySpaceOffersAddPlugin() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QWidget* container = row->parentWidget();
    QVERIFY(container);

    bool hasAdd = false;
    bool hasRemove = false;
    QTimer::singleShot(0, [&hasAdd, &hasRemove] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Add Plugin")) hasAdd = true;
            if (a->text().contains("Remove")) hasRemove = true;
        }
        menu->close();
    });

    // Right-click empty space below the last row.
    QPoint emptyPos(container->width() / 2, row->geometry().bottom() + 20);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, emptyPos,
                         container->mapToGlobal(emptyPos));
    QApplication::sendEvent(container, &ev);
    QCoreApplication::processEvents();

    QVERIFY(hasAdd);
    QVERIFY(!hasRemove); // no row under the cursor
}

void MainWindowTest::pluginListContextMenuRowOffersAddAndRemove() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginListWidget list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 120);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();

    bool hasAdd = false;
    bool hasRemove = false;
    QTimer::singleShot(0, [&hasAdd, &hasRemove] {
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions()) {
            if (a->text().contains("Add Plugin")) hasAdd = true;
            if (a->text().contains("Remove")) hasRemove = true;
        }
        menu->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, row->rect().center(),
                         row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &ev);
    QCoreApplication::processEvents();

    QVERIFY(hasAdd);
    QVERIFY(hasRemove);
}

void MainWindowTest::pluginListContextMenuAddOpensPicker() {
    AudioBus bus;
    bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    PluginManager manager;
    PluginListWidget list;
    list.setBus(&bus);
    list.setPluginManager(&manager);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    QPushButton* enableBtn = nullptr;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON") { enableBtn = b; break; }
    QVERIFY(enableBtn);
    QWidget* row = enableBtn->parentWidget();
    QWidget* container = row->parentWidget();
    QVERIFY(container);

    bool dialogSeen = false;
    QTimer::singleShot(0, [&dialogSeen] {
        // The "Add Plugin..." action opens a modal picker dialog; the second
        // timer runs inside that dialog's event loop.
        QTimer::singleShot(0, [&dialogSeen] {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* dlg = qobject_cast<QDialog*>(w)) {
                    if (dlg->findChild<QListWidget*>()) {
                        dialogSeen = true;
                        dlg->reject();
                        return;
                    }
                }
            }
        });
        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w)) { menu = m; break; }
        if (!menu) return;
        for (QAction* a : menu->actions())
            if (a->text().contains("Add Plugin")) { a->trigger(); break; }
        menu->close();
    });

    QPoint emptyPos(container->width() / 2, row->geometry().bottom() + 20);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, emptyPos,
                         container->mapToGlobal(emptyPos));
    QApplication::sendEvent(container, &ev);
    QCoreApplication::processEvents();

    QVERIFY(dialogSeen);
}

void MainWindowTest::pluginListDragShowsInsertionLine() {
    static const char* const kMimePluginIndex = "application/x-vvvdaw-plugin-index";

    // Exposes the protected drag handlers so the widget's own drag logic can
    // be exercised directly. Qt's global QDragManager intercepts synthetic
    // drag events sent via QApplication::sendEvent, so we call the handlers
    // the way the drag state machine would.
    class TestablePluginList : public PluginListWidget {
    public:
        using PluginListWidget::dragMoveEvent;
        using PluginListWidget::dragLeaveEvent;
        using PluginListWidget::dropEvent;
    };

    AudioBus bus;
    for (int i = 0; i < 3; ++i)
        bus.pluginChain().addPlugin(std::make_unique<StubSynth>());

    TestablePluginList list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    // Locate the rendered rows (the widget holding each ON/OFF button).
    std::vector<QWidget*> rows;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    auto* line = list.findChild<QFrame*>("pluginInsertionLine");
    QVERIFY(line);
    QVERIFY(!line->isVisible());

    // Drag plugin 0 over the upper half of the third row: the line must sit at
    // the boundary above that row (container coordinates), full width.
    QMimeData mime;
    mime.setData(kMimePluginIndex, QByteArray::number(0));
    QPoint listPos = rows[2]->mapTo(&list, QPoint(0, 2));
    QDragMoveEvent moveEv(listPos, Qt::MoveAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    list.dragMoveEvent(&moveEv);

    QVERIFY(line->isVisible());
    QCOMPARE(line->x(), 0);
    QCOMPARE(line->height(), 2);
    QCOMPARE(line->y(), rows[2]->pos().y() - 1);
    QVERIFY(line->width() >= rows[0]->width());

    // Leaving the list hides the line again.
    QDragLeaveEvent leaveEv;
    list.dragLeaveEvent(&leaveEv);
    QVERIFY(!line->isVisible());
}

void MainWindowTest::pluginListDropFollowsInsertionLine() {
    static const char* const kMimePluginIndex = "application/x-vvvdaw-plugin-index";

    class TestablePluginList : public PluginListWidget {
    public:
        using PluginListWidget::dropEvent;
    };

    AudioBus bus;
    PluginInstance* p0 = nullptr;
    PluginInstance* p1 = nullptr;
    PluginInstance* p2 = nullptr;
    for (int i = 0; i < 3; ++i) {
        auto synth = std::make_unique<StubSynth>();
        if (i == 0) p0 = synth.get();
        if (i == 1) p1 = synth.get();
        if (i == 2) p2 = synth.get();
        bus.pluginChain().addPlugin(std::move(synth));
    }

    TestablePluginList list;
    list.setBus(&bus);
    list.rebuild();
    list.resize(240, 200);
    list.show();
    QCoreApplication::processEvents();

    std::vector<QWidget*> rows;
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    // Drop plugin 0 at the upper half of the third row: the boundary is before
    // the third row, so the order becomes p1, p0, p2.
    QMimeData mime;
    mime.setData(kMimePluginIndex, QByteArray::number(0));
    QPoint listPos = rows[2]->mapTo(&list, QPoint(0, 2));
    QDropEvent dropEv(QPointF(listPos), Qt::MoveAction, &mime,
                      Qt::LeftButton, Qt::NoModifier);
    list.dropEvent(&dropEv);

    QCOMPARE(bus.pluginChain().plugin(0), p1);
    QCOMPARE(bus.pluginChain().plugin(1), p0);
    QCOMPARE(bus.pluginChain().plugin(2), p2);

    // Dropping below the last row appends the dragged plugin at the end.
    list.rebuild();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    rows.clear();
    for (QPushButton* b : list.findChildren<QPushButton*>())
        if (b->text() == "ON" || b->text() == "OFF")
            rows.push_back(b->parentWidget());
    QCOMPARE(static_cast<int>(rows.size()), 3);

    QMimeData mime2;
    mime2.setData(kMimePluginIndex, QByteArray::number(0)); // drag p1 (now at index 0)
    QPoint below = rows[2]->mapTo(&list, QPoint(0, rows[2]->height()));
    QDropEvent dropBelow(QPointF(below), Qt::MoveAction, &mime2,
                         Qt::LeftButton, Qt::NoModifier);
    list.dropEvent(&dropBelow);

    // Order was [p1, p0, p2]; dragging p1 past the end appends it.
    QCOMPARE(bus.pluginChain().plugin(0), p0);
    QCOMPARE(bus.pluginChain().plugin(1), p2);
    QCOMPARE(bus.pluginChain().plugin(2), p1);
}

void MainWindowTest::pluginWindowStaysOnTop() {
    auto plugin = std::make_unique<StubSynth>();
    PluginWindow window(plugin.get(), 3, nullptr);
    QVERIFY(window.windowFlags() & Qt::WindowStaysOnTopHint);
}

void MainWindowTest::pianoRollWindowStaysOnTop() {
    Project project;
    project.addMidiTrack("Midi 1");
    Track& track = project.tracks()[0];
    auto clip = std::make_shared<MidiClip>();
    clip->addNote(60, 100, 0, 960);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addMidiEvent(ev);
    const int64_t id = track.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.openPianoRoll(0, id);

    QCOMPARE(window.m_pianoRollWindows.size(), size_t(1));
    QVERIFY(window.m_pianoRollWindows[0]->windowFlags() & Qt::WindowStaysOnTopHint);
}

void MainWindowTest::mainWindowRestoresPanelStateFromSettings() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    settings.busPanelVisible = true;
    settings.busPanelHeight = 320;
    settings.instrumentPanelVisible = true;
    settings.instrumentPanelHeight = 260;

    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.m_busPanel->isHidden());
    QCOMPARE(window.m_busPanel->maximumHeight(), 320);
    QVERIFY(!window.m_instrumentPanel->isHidden());
    QCOMPARE(window.m_instrumentPanel->maximumHeight(), 260);
    QVERIFY(!window.m_busPanelGrip->isHidden());
    QVERIFY(!window.m_instrumentPanelGrip->isHidden());

    // The restored panels must actually contain their widgets (rows built
    // during construction), not render empty until toggled.
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("soloButton").size(),
             project.buses().size());
    bool foundSynthButton = false;
    for (QPushButton* b : window.m_instrumentPanel->findChildren<QPushButton*>())
        if (b->text() == "No Synth") foundSynthButton = true;
    QVERIFY(foundSynthButton);

    // The View menu checkmarks reflect the restored visibility.
    bool busChecked = false, instChecked = false;
    for (auto* menuAction : window.menuBar()->actions()) {
        auto* menu = menuAction->menu();
        if (!menu) continue;
        for (auto* action : menu->actions()) {
            if (action->text().contains("Bus Panel")) busChecked = action->isChecked();
            if (action->text().contains("Instrument Panel")) instChecked = action->isChecked();
        }
    }
    QVERIFY(busChecked);
    QVERIFY(instChecked);
}

void MainWindowTest::mainWindowRestoresSizeFromSettings() {
    Project project;
    Settings settings;
    settings.mainWindowWidth = 1100;
    settings.mainWindowHeight = 650;

    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QCOMPARE(window.size().width(), 1100);
    QCOMPARE(window.size().height(), 650);
}

void MainWindowTest::panelTogglesAndGripUpdateSettings() {
    Project project;
    Settings settings; // both panels hidden, default heights
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(window.m_busPanel->isHidden());
    QVERIFY(!settings.busPanelVisible);

    QAction* busAction = nullptr;
    for (auto* menuAction : window.menuBar()->actions()) {
        auto* menu = menuAction->menu();
        if (!menu) continue;
        for (auto* action : menu->actions()) {
            if (action->text().contains("Bus Panel"))
                busAction = action;
        }
    }
    QVERIFY(busAction);
    busAction->trigger();
    QVERIFY(settings.busPanelVisible);
    QVERIFY(!window.m_busPanel->isHidden());

    // Dragging the bus grip writes the new height into settings.
    const int startHeight = settings.busPanelHeight;
    window.show();
    window.resize(1000, 800);
    QCoreApplication::processEvents();

    QWidget* grip = window.m_busPanelGrip;
    QVERIFY(!grip->isHidden());
    const QPoint gripCenter = grip->rect().center();
    const QPoint above = gripCenter - QPoint(0, 40);

    QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, gripCenter);
    QTest::mouseMove(grip, above);
    QTest::mouseRelease(grip, Qt::LeftButton, Qt::NoModifier, above);
    QCoreApplication::processEvents();

    // The drag moved the handle upward, increasing the panel height.
    QVERIFY(settings.busPanelHeight > startHeight);
}

void MainWindowTest::busRenameRefreshesTrackOutCombo() {
    Project project;
    project.addTrack("Audio 1");

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QComboBox* audioOut = findComboContaining(window.m_trackRows[0].panel, "Metronome");
    QVERIFY(audioOut);

    window.m_project.buses()[1].setName("FX Bus");
    emit window.m_busPanel->busNameWillChange(1, "Metronome", "FX Bus");

    QVERIFY(findComboContaining(window.m_trackRows[0].panel, "FX Bus"));
    QVERIFY(!findComboContaining(window.m_trackRows[0].panel, "Metronome"));
}

void MainWindowTest::instrumentRenameRefreshesMidiTrackOutCombo() {
    Project project;
    project.addMidiTrack("Midi 1");
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));
    project.tracks()[0].setInstrumentIndex(0);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QComboBox* midiOut = findComboContaining(window.m_trackRows[0].panel, "Inst:");
    QVERIFY(midiOut);
    QVERIFY(midiOut->currentText().contains("Inst: Pad"));

    window.m_project.instruments()[0].setName("Lead");
    emit window.m_instrumentPanel->nameWillChange(0, "Pad", "Lead");

    midiOut = findComboContaining(window.m_trackRows[0].panel, "Inst: Lead");
    QVERIFY(midiOut);
    QVERIFY(midiOut->currentText().contains("Inst: Lead"));
}

void MainWindowTest::busRenameRefreshesBusAndInstrumentOutCombos() {
    Project project;
    Instrument inst;
    inst.setName("Pad");
    inst.setOutputBusIndex(1);
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    window.m_busPanel->rebuild();
    window.m_instrumentPanel->rebuild();

    QVERIFY(findComboContaining(window.m_busPanel, "Metronome"));
    QVERIFY(findComboContaining(window.m_instrumentPanel, "Metronome"));

    window.m_project.buses()[1].setName("FX Bus");
    emit window.m_busPanel->busNameWillChange(1, "Metronome", "FX Bus");

    QVERIFY(findComboContaining(window.m_busPanel, "FX Bus"));
    QVERIFY(findComboContaining(window.m_instrumentPanel, "FX Bus"));
}

void MainWindowTest::rejectAudioEventToMidiTrack() {
    Project project;
    project.addTrack("A1");
    project.addMidiTrack("M1");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    AudioEvent ev;
    ev.setStartSample(100);
    src.addEvent(ev);
    const int64_t id = src.events().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.moveEventToTrack(0, 1, id, 500));
    QCOMPARE(src.events().size(), size_t(1));
    QCOMPARE(src.events().front().startSample(), int64_t(100));
    QVERIFY(dst.events().empty());
    QVERIFY(dst.midiEvents().empty());
}

void MainWindowTest::rejectMidiEventToAudioTrack() {
    Project project;
    project.addMidiTrack("M1");
    project.addTrack("A1");
    Track& src = project.tracks()[0];
    Track& dst = project.tracks()[1];
    MidiEvent ev;
    ev.setStartSample(100);
    src.addMidiEvent(ev);
    const int64_t id = src.midiEvents().front().id();

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    QVERIFY(!window.moveEventToTrack(0, 1, id, 500));
    QCOMPARE(src.midiEvents().size(), size_t(1));
    QCOMPARE(src.midiEvents().front().startSample(), int64_t(100));
    QVERIFY(dst.events().empty());
    QVERIFY(dst.midiEvents().empty());
}

void MainWindowTest::startDialogListsRecentProjects() {
    const QString oldPath = m_tmpDir->filePath("project_old.json");
    const QString recentPath = m_tmpDir->filePath("project_recent.json");
    QFile oldFile(oldPath);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    oldFile.write("{}");
    QFile recentFile(recentPath);
    QVERIFY(recentFile.open(QIODevice::WriteOnly));
    recentFile.write("{}");

    Settings settings;
    settings.addRecentProject(oldPath);
    settings.addRecentProject(recentPath);

    StartDialog dialog(settings);
    QCOMPARE(dialog.m_recentList->count(), 2);
    QCOMPARE(dialog.m_recentList->item(0)->data(Qt::UserRole).toString(),
             recentPath);
    QCOMPARE(dialog.m_recentList->item(1)->data(Qt::UserRole).toString(),
             oldPath);
}

void MainWindowTest::startDialogListsTemplates() {
    Settings settings;
    StartDialog dialog(settings);
    QVERIFY(dialog.m_templateList->count() >= 2);
    QStringList texts;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        texts << dialog.m_templateList->item(i)->data(Qt::UserRole).toString();
    QVERIFY(texts.contains("empty"));
    QVERIFY(texts.contains("rock-band"));
}

void MainWindowTest::startDialogSelectingTemplateSetsChoice() {
    Settings settings;
    StartDialog dialog(settings);
    dialog.show();
    QCoreApplication::processEvents();

    int idx = -1;
    for (int i = 0; i < dialog.m_templateList->count(); ++i)
        if (dialog.m_templateList->item(i)->data(Qt::UserRole).toString() == "empty")
            idx = i;
    QVERIFY(idx >= 0);
    dialog.m_templateList->setCurrentRow(idx);
    dialog.m_useTemplateButton->click();

    QCOMPARE(dialog.choice().action, StartDialog::Action::OpenTemplate);
    QCOMPARE(dialog.choice().templateName, QString("empty"));
}

void MainWindowTest::mainWindowFileMenuHasSaveAsTemplate() {
    Project project;
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    auto* fileMenu = window.menuBar()->actions().value(0)->menu();
    QVERIFY(fileMenu);
    bool found = false;
    for (auto* action : fileMenu->actions()) {
        if (action->text().contains("Template")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void MainWindowTest::replaceProjectSwapsAndRebuilds() {
    Project project;
    project.addTrack("T1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_project.tracks().size(), size_t(1));
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    Project fresh;
    fresh.addTrack("A");
    fresh.addTrack("B");
    const QString freshName = fresh.name();
    window.replaceProject(std::move(fresh));

    QCOMPARE(window.m_project.tracks().size(), size_t(2));
    QCOMPARE(window.m_project.tracks()[0].name(), QString("A"));
    QCOMPARE(window.m_project.tracks()[1].name(), QString("B"));
    QCOMPARE(window.m_project.name(), freshName);
    QCOMPARE(window.m_trackRows.size(), size_t(2));
    QVERIFY(window.m_project.filePath().isEmpty());
}

void MainWindowTest::midiTrackShowsArmButton() {
    Project project;
    project.addMidiTrack("Midi 1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    QCOMPARE(window.m_trackRows.size(), size_t(1));

    TrackPanelWidget* panel = window.m_trackRows[0].panel;
    QVERIFY(panel);
    QCOMPARE(panel->track(), &project.tracks()[0]);

    // MIDI tracks expose the record-arm button so input can be captured.
    auto* arm = panel->findChild<QPushButton*>("armButton");
    QVERIFY(arm);
    QVERIFY(arm->isVisibleTo(panel));

    arm->click();
    QVERIFY(project.tracks()[0].isRecordArmed());
    arm->click();
    QVERIFY(!project.tracks()[0].isRecordArmed());
}

void MainWindowTest::settingsDialogHasMidiInputControls() {
    Project project;
    Settings settings;
    AudioEngine engine;
    SettingsDialog dialog(settings, engine);

    auto* midiCombo = dialog.findChild<QComboBox*>("midiInputCombo");
    QVERIFY(midiCombo);
    QVERIFY(midiCombo->count() >= 1);
    QCOMPARE(midiCombo->itemData(0).toInt(), -1); // "None" default

    auto* typeCombo = dialog.findChild<QComboBox*>("midiTransportTypeCombo");
    QVERIFY(typeCombo);
    QCOMPARE(typeCombo->currentData().toInt(), settings.midiTransportControlType);

    auto* channelCombo = dialog.findChild<QComboBox*>("midiChannelCombo");
    QVERIFY(channelCombo);
    QCOMPARE(channelCombo->currentData().toInt(), settings.midiTransportChannel);

    auto* playSpin = dialog.findChild<QSpinBox*>("midiPlaySpin");
    QVERIFY(playSpin);
    QCOMPARE(playSpin->value(), settings.midiTransportPlayControl);
    auto* recordSpin = dialog.findChild<QSpinBox*>("midiRecordSpin");
    QVERIFY(recordSpin);
    QCOMPARE(recordSpin->value(), settings.midiTransportRecordControl);
    auto* stopSpin = dialog.findChild<QSpinBox*>("midiStopSpin");
    QVERIFY(stopSpin);
    QCOMPARE(stopSpin->value(), settings.midiTransportStopControl);
}

void MainWindowTest::settingsDialogLearnFlow() {
    Project project;
    Settings settings;
    AudioEngine engine;
    SettingsDialog dialog(settings, engine);

    auto* midiCombo = dialog.findChild<QComboBox*>("midiInputCombo");
    auto* playBtn = dialog.findChild<QPushButton*>("midiLearnPlayBtn");
    auto* recordBtn = dialog.findChild<QPushButton*>("midiLearnRecordBtn");
    auto* stopBtn = dialog.findChild<QPushButton*>("midiLearnStopBtn");
    QVERIFY(midiCombo);
    QVERIFY(playBtn);
    QVERIFY(recordBtn);
    QVERIFY(stopBtn);

    // With no device selected, Learn must not enter learning state.
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::None);

    if (midiCombo->count() < 2)
        QSKIP("No MIDI input device available on this machine");

    midiCombo->setCurrentIndex(1);
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::Play);

    // Clicking the same button again cancels the learning.
    playBtn->click();
    QCOMPARE(engine.midiLearnTarget(), MidiLearnTarget::None);
}

void MainWindowTest::previewTargetFollowsFocusedPianoRoll() {
    Project project;
    project.addMidiTrack("Midi 1");
    project.addMidiTrack("Midi 2");

    auto addEvent = [&project](int trackIndex) -> int64_t {
        auto clip = std::make_shared<MidiClip>();
        clip->setLengthTicks(MidiClip::kPPQ);
        MidiEvent ev;
        ev.setClip(clip);
        ev.setStartSample(0);
        ev.setDurationSample(48000);
        project.tracks()[static_cast<size_t>(trackIndex)].addMidiEvent(ev);
        return project.tracks()[static_cast<size_t>(trackIndex)].midiEvents().back().id();
    };
    const int64_t id0 = addEvent(0);
    const int64_t id1 = addEvent(1);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);

    // No piano roll open: nothing to preview into.
    QCOMPARE(engine.midiPreviewTrack(), -1);

    // Opening the first piano roll routes preview into its track.
    window.openPianoRoll(0, id0);
    QCOMPARE(window.m_pianoRollWindows.size(), size_t(1));
    QCOMPARE(engine.midiPreviewTrack(), 0);

    // Opening the second makes it the active preview target.
    window.openPianoRoll(1, id1);
    QCOMPARE(window.m_pianoRollWindows.size(), size_t(2));
    QCOMPARE(engine.midiPreviewTrack(), 1);

    // Switching focus back (what the WindowActivate filter calls) retargets.
    window.setActiveMidiPreview(0, id0);
    QCOMPARE(engine.midiPreviewTrack(), 0);
}

void MainWindowTest::middleDragPansTrackView() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.002);            // 1 px = 500 samples
    view->setScrollOffset(48000);

    // Drag right with the middle button: the content follows the cursor, so
    // the scroll offset decreases.
    QTest::mousePress(view, Qt::MiddleButton, Qt::NoModifier, QPoint(100, 40));
    QTest::mouseMove(view, QPoint(150, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 - 50 * 500));

    // Drag left: the offset tracks the total drag delta from the press.
    QTest::mouseMove(view, QPoint(50, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 50 * 500));
    QTest::mouseMove(view, QPoint(0, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 100 * 500));

    // Releasing the middle button stops the pan: moves no longer scroll.
    QTest::mouseRelease(view, Qt::MiddleButton, Qt::NoModifier, QPoint(0, 40));
    QTest::mouseMove(view, QPoint(300, 40));
    QCOMPARE(view->scrollOffset(), int64_t(48000 + 100 * 500));
}

void MainWindowTest::ctrlWheelZoomAnchorsCursorFrame() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);
    view->setScrollOffset(0);

    const QPoint pos(200, 40);
    const int64_t before = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());

    auto sendZoomWheel = [view](const QPoint& p, int deltaY) {
        QWheelEvent ev(QPointF(p), QPointF(p), QPoint(0, 0), QPoint(0, deltaY),
                       Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view, &ev);
    };

    // First zoom-in notch: the frame under the cursor must not move.
    sendZoomWheel(pos, 120);
    QVERIFY(view->zoom() > 0.001);
    QVERIFY(view->scrollOffset() != 0);
    const int64_t after1 = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());
    QCOMPARE(after1, before);

    // A second notch keeps the same frame anchored.
    sendZoomWheel(pos, 120);
    const int64_t after2 = view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom());
    QCOMPARE(after2, before);

    // And zooming back out restores the original view.
    sendZoomWheel(pos, -120);
    sendZoomWheel(pos, -120);
    QCOMPARE(view->scrollOffset() + static_cast<int64_t>(pos.x() / view->zoom()), before);
}

void MainWindowTest::trackViewMouseCursorTracksAndClears() {
    Project project;
    project.addTrack("A1");
    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);

    QCOMPARE(view->mouseCursorX(), -1);

    QTest::mouseMove(view, QPoint(150, 40));
    QCOMPARE(view->mouseCursorX(), 150);

    QTest::mouseMove(view, QPoint(37, 40));
    QCOMPARE(view->mouseCursorX(), 37);

    // Leaving the widget clears the thin cursor line.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view, &leave);
    QCOMPARE(view->mouseCursorX(), -1);
}

void MainWindowTest::trackViewContextMenuCutSplitsEvent() {
    Project project;
    project.addTrack("A1");
    Track& track = project.tracks()[0];
    AudioEvent ev;
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    track.addEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    QCoreApplication::processEvents();

    TrackViewWidget* view = window.m_trackRows[0].view;
    view->resize(400, 80);
    view->setZoom(0.001);       // 1 px = 1000 samples
    view->setScrollOffset(0);

    // Event spans samples [0, 48000) => pixels [0, 48). Right-click in the
    // middle at pixel 24 => sample 24000 (no grid snap).
    const QPoint menuPos(24, 40);
    const int64_t cutSample = 24000;

    bool cutFound = false;
    QTimer::singleShot(0, [&] {
        if (auto* menu = view->findChild<QMenu*>()) {
            for (QAction* a : menu->actions()) {
                if (a->text() == "Cut") {
                    a->trigger();
                    cutFound = true;
                    break;
                }
            }
            // A programmatic trigger() does not dismiss the popup; close it so
            // menu.exec() returns and the pending cut is emitted.
            menu->close();
        }
    });

    QContextMenuEvent ctx(QContextMenuEvent::Mouse, menuPos,
                          view->mapToGlobal(menuPos));
    QApplication::sendEvent(view, &ctx);
    QCoreApplication::processEvents();

    QVERIFY(cutFound);
    QCOMPARE(project.tracks()[0].events().size(), size_t(2));
    QCOMPARE(project.tracks()[0].events()[0].endSample(),
             project.tracks()[0].events()[1].startSample());
    QCOMPARE(project.tracks()[0].events()[1].startSample(), cutSample);
}

void MainWindowTest::pianoRollMiddleDragPans() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 16); // content wider than the viewport
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* widget = window.m_pianoRollWindows[0]->findChild<PianoRollWidget*>();
    QVERIFY(widget);
    auto* scrollArea = widget->enclosingScrollArea();
    QVERIFY(scrollArea);
    QScrollBar* hbar = scrollArea->horizontalScrollBar();
    QVERIFY(hbar->maximum() > 0); // there is room to scroll

    hbar->setValue(50);
    QCOMPARE(hbar->value(), 50);

    // Drag right with the middle button: content follows, scroll decreases.
    QTest::mousePress(widget, Qt::MiddleButton, Qt::NoModifier, QPoint(300, 100));
    QTest::mouseMove(widget, QPoint(340, 100));
    QCOMPARE(hbar->value(), 10);
    QTest::mouseMove(widget, QPoint(300, 100));
    QCOMPARE(hbar->value(), 50);
    QTest::mouseMove(widget, QPoint(250, 100));
    QCOMPARE(hbar->value(), 100);

    // After release, moves no longer pan.
    QTest::mouseRelease(widget, Qt::MiddleButton, Qt::NoModifier, QPoint(250, 100));
    QTest::mouseMove(widget, QPoint(350, 100));
    QCOMPARE(hbar->value(), 100);
}

void MainWindowTest::pianoRollCtrlWheelZoomAnchorsCursor() {
    Project project;
    project.addMidiTrack("Midi 1");
    auto clip = std::make_shared<MidiClip>();
    clip->setLengthTicks(MidiClip::kPPQ * 16);
    MidiEvent ev;
    ev.setClip(clip);
    ev.setStartSample(0);
    ev.setDurationSample(48000);
    project.tracks()[0].addMidiEvent(ev);

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.show();
    window.openPianoRoll(0, project.tracks()[0].midiEvents()[0].id());
    QCoreApplication::processEvents();

    auto* widget = window.m_pianoRollWindows[0]->findChild<PianoRollWidget*>();
    QVERIFY(widget);
    auto* scrollArea = widget->enclosingScrollArea();
    QVERIFY(scrollArea);
    QScrollBar* hbar = scrollArea->horizontalScrollBar();
    QVERIFY(hbar->maximum() > 0);

    const double oldPps = 0.06; // PianoRollWidget default
    hbar->setValue(100);
    const int viewportX = 300; // cursor position within the viewport
    const int mouseX = viewportX + 100;

    QWheelEvent we(QPointF(mouseX, 100), QPointF(mouseX, 100), QPoint(0, 0), QPoint(0, 120),
                   Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &we);
    QCoreApplication::processEvents();

    const double newPps = std::clamp(oldPps * 1.2, 0.004, 2.0);
    QVERIFY(newPps > oldPps);
    const double anchorTick = static_cast<double>(mouseX - 56) / oldPps;
    const int expected = std::max(0, static_cast<int>(std::lround(
        anchorTick * newPps - viewportX + 56)));
    QCOMPARE(hbar->value(), expected);
}

void MainWindowTest::panSliderHighlightsDeviationFromCenter() {
    PanSlider slider;
    slider.setRange(-100, 100);
    slider.resize(100, PanSlider::kHeight);

    // At the center value the highlight collapses to a degenerate span.
    slider.setValue(0);
    const QRect zero = slider.highlightRect();
    QCOMPARE(zero.width(), 0);
    const int center = zero.left();
    QCOMPARE(slider.toolTip(), QString("Pan: Center"));

    // A positive value highlights the span from the center to the right.
    slider.setValue(50);
    const QRect right = slider.highlightRect();
    QVERIFY(right.width() > 4);
    QCOMPARE(right.left(), center);
    QVERIFY(right.center().x() > center);
    QCOMPARE(slider.toolTip(), QString("Pan: R 50%"));

    // A negative value highlights the span from the left to the center, an
    // exact mirror of the positive case around the same center point.
    slider.setValue(-50);
    const QRect left = slider.highlightRect();
    QVERIFY(left.width() > 4);
    QCOMPARE(left.right(), center - 1);
    QVERIFY(left.center().x() < center);
    QCOMPARE(slider.toolTip(), QString("Pan: L 50%"));

    QVERIFY(std::abs(left.width() - right.width()) <= 1);
    QCOMPARE(left.height(), right.height());
    QCOMPARE(left.bottom(), right.bottom());
}

void MainWindowTest::sliderSizesAreIncreasedForUsability() {
    Project project;
    project.addTrack("Audio 1");
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));
    Instrument inst;
    inst.setName("Pad");
    project.addInstrument(std::move(inst));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();
    window.m_instrumentPanel->rebuild();
    window.show();
    QCoreApplication::processEvents();

    constexpr int kMinSliderExtent = 16;

    // Track panel: horizontal pan + volume sliders are taller.
    QCOMPARE(window.m_trackRows.size(), size_t(1));
    TrackPanelWidget* trackPanel = window.m_trackRows[0].panel;
    const auto trackSliders = trackPanel->findChildren<QSlider*>();
    QCOMPARE(trackSliders.size(), 2); // pan + volume
    for (QSlider* s : trackSliders)
        QVERIFY2(s->height() >= kMinSliderExtent, "track slider too small");

    // Volume slider tooltip reports the current percentage.
    QSlider* trackVol = nullptr;
    QSlider* trackPan = nullptr;
    for (QSlider* s : trackSliders) {
        if (s->maximum() == 100 && s->minimum() == 0) trackVol = s;
        else trackPan = s;
    }
    QVERIFY(trackVol);
    QVERIFY(trackPan);
    trackVol->setValue(75);
    QCOMPARE(trackVol->toolTip(), QString("Volume: 75%"));

    // Bus panel: vertical volume fader is wider, pan slider is taller.
    const auto busVol = window.m_busPanel->findChildren<QSlider*>("volumeSlider");
    QCOMPARE(busVol.size(), 3);
    for (QSlider* s : busVol)
        QVERIFY2(s->width() >= 28, "bus volume slider too narrow");

    // Bus volume fader tooltip reports the dB value (midpoint = -30 dB).
    busVol[0]->setValue(50);
    QCOMPARE(busVol[0]->toolTip(), QString("Volume: -30.0 dB"));
    const auto busPan = window.m_busPanel->findChildren<PanSlider*>();
    QCOMPARE(busPan.size(), 3);
    for (PanSlider* s : busPan)
        QVERIFY2(s->height() >= kMinSliderExtent, "bus pan slider too small");

    // Instrument panel: horizontal pan + volume sliders are taller.
    const auto instSliders = window.m_instrumentPanel->findChildren<QSlider*>();
    QCOMPARE(instSliders.size(), 2); // pan + volume
    for (QSlider* s : instSliders)
        QVERIFY2(s->height() >= kMinSliderExtent, "instrument slider too small");
}

QTEST_MAIN(MainWindowTest)
#include "test_gui.moc"
