#include <QTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QMenuBar>
#include <QListWidget>
#include <QTableWidget>
#include <algorithm>
#include <memory>
#include <portaudio.h>

#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "plugin/PluginInstance.h"
#include "gui/MainWindow.h"
#include "gui/StartDialog.h"
#include "gui/TrackPanelWidget.h"
#include "gui/TrackViewWidget.h"
#include "gui/TimelineRuler.h"
#include "gui/MeasureRuler.h"
#include "gui/BusPanelWidget.h"
#include "gui/BusLevelMeter.h"
#include "gui/InstrumentPanelWidget.h"
#include "gui/PluginListWidget.h"
#include "gui/ChannelRoutingDialog.h"

namespace {
QComboBox* findComboContaining(QWidget* parent, const QString& text) {
    const auto combos = parent->findChildren<QComboBox*>();
    for (QComboBox* cb : combos)
        for (int i = 0; i < cb->count(); ++i)
            if (cb->itemText(i).contains(text))
                return cb;
    return nullptr;
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
    void audioTrackOutComboListsBuses();
    void busPanelStripHasCompactControls();
    void busPanelStripsStayFixedWidth();
    void busPanelPluginToggleRevealsPluginList();
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
        QVERIFY(!midiOut->itemText(i).contains("Master"));
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

    const auto toggles = window.m_busPanel->findChildren<QPushButton*>("pluginToggle");
    QCOMPARE(toggles.size(), 3);
    for (QPushButton* b : toggles)
        QVERIFY(b->isCheckable());

    // S/M buttons live below the name (one pair per bus).
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("soloButton").size(), 3);
    QCOMPARE(window.m_busPanel->findChildren<QPushButton*>("muteButton").size(), 3);

    // The collapsed strip is much narrower than before.
    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    QVERIFY(strip->sizeHint().width() <= 100);

    // Plugin lists exist but are hidden until toggled.
    const auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(lists.size(), 3);
    for (QWidget* l : lists)
        QVERIFY(l->isHidden());
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
        const auto toggles = window.m_busPanel->findChildren<QPushButton*>("pluginToggle");
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

void MainWindowTest::busPanelPluginToggleRevealsPluginList() {
    Project project;
    AudioBus b1;
    b1.setName("FX");
    project.addBus(std::move(b1));

    Settings settings;
    AudioEngine engine;
    MainWindow window(project, engine, settings);
    window.m_busPanel->rebuild();

    auto toggles = window.m_busPanel->findChildren<QPushButton*>("pluginToggle");
    auto lists = window.m_busPanel->findChildren<PluginListWidget*>("busPluginList");
    QCOMPARE(toggles.size(), 3);
    QCOMPARE(lists.size(), 3);

    QWidget* strip = toggles[0]->parentWidget()->parentWidget();
    const int collapsedWidth = strip->sizeHint().width();

    QVERIFY(lists[0]->isHidden());
    toggles[0]->click();
    QVERIFY(!lists[0]->isHidden());
    QVERIFY(strip->sizeHint().width() > collapsedWidth); // strip widens

    toggles[0]->click();
    QVERIFY(lists[0]->isHidden());
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

QTEST_MAIN(MainWindowTest)
#include "test_gui.moc"
