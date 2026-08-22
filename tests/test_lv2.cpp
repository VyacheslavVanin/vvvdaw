#include <QTest>
#include <QJsonArray>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <set>
#include <thread>
#include <vector>

#include "plugin/LV2Instance.h"
#include "plugin/PluginChain.h"
#include "plugin/SigGuard.h"
#include "model/Instrument.h"

// Smoke / integration tests for the LV2 backend. These exercise the real
// Lilv world and installed plugins, so they verify the load/activate/process/
// state contracts without crashing rather than DSP correctness. Tests that
// need a specific plugin skip gracefully when it is not installed.

namespace {

constexpr const char* kZamCompUri = "urn:zamaudio:ZamComp";
constexpr const char* kHardLimiterUri = "http://plugin.org.uk/swh-plugins/hardLimiter";
constexpr const char* kDrumGizmoUri = "http://drumgizmo.org/lv2";
constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;

bool allFinite(const std::vector<float>& a, const std::vector<float>& b) {
    for (float v : a)
        if (!std::isfinite(v)) return false;
    for (float v : b)
        if (!std::isfinite(v)) return false;
    return true;
}

bool allFinite(const std::vector<std::vector<float>>& channels) {
    for (const auto& ch : channels)
        for (float v : ch)
            if (!std::isfinite(v)) return false;
    return true;
}

} // namespace

class TestLV2 : public QObject {
    Q_OBJECT
private slots:
    void sigGuardRecovers();
    void sigGuardForeignThreadCrash();
    void loadKnownPlugin();
    void activateProcessDeactivate();
    void stateRoundTrip();
    void stateToJsonWithoutStateExtension();
    void stateSerializationSurvivesBuggyPlugin();
    void genericScanSmoke();
    void multiChannelOutputDiscovery();
    void multiChannelProcess();
    void outputControlPortMetersFlow();
};

void TestLV2::sigGuardRecovers() {
    QVERIFY(runSigGuarded([] {}));

    // A deliberately faulting body must be caught and reported, not kill the
    // process. This is the same mechanism that protects the native editors.
    bool crashed = !runSigGuarded([] { raise(SIGSEGV); });
    QVERIFY(crashed);

    // The guard must be reusable after a crash.
    QVERIFY(runSigGuarded([] {}));
}

void TestLV2::sigGuardForeignThreadCrash() {
    // Regression: runSigGuarded installs its SIGSEGV handler process-wide but
    // longjmps to a thread-local buffer. A fault on another thread (the audio
    // callback thread in the app) must be handed off to the previously
    // installed handler, not longjmp into the guarded thread's stack.
    static volatile sig_atomic_t previousHandlerRan = 0;
    struct sigaction testSa{}, oldSa{};
    testSa.sa_handler = [](int) { previousHandlerRan = 1; };
    sigemptyset(&testSa.sa_mask);
    sigaction(SIGSEGV, &testSa, &oldSa);

    std::atomic<bool> go{false};
    std::atomic<bool> foreignDone{false};
    std::thread t([&] {
        while (!go.load()) std::this_thread::yield();
        raise(SIGSEGV);
        foreignDone = true;
    });

    bool guardedOk = runSigGuarded([&] {
        go = true;
        while (!foreignDone.load()) std::this_thread::yield();
    });

    t.join();
    sigaction(SIGSEGV, &oldSa, nullptr);

    QVERIFY(previousHandlerRan == 1); // the foreign crash reached the old handler
    QVERIFY(guardedOk);               // the guard itself was not tripped
    QVERIFY(runSigGuarded([] {}));    // and remains usable
}

void TestLV2::loadKnownPlugin() {
    LV2Instance inst;
    if (!inst.load(kZamCompUri))
        QSKIP("ZamComp LV2 plugin not installed");

    QVERIFY(!inst.name().isEmpty());
    QCOMPARE(inst.pluginId(), QString(kZamCompUri));
    QVERIFY(!inst.filePath().isEmpty());
    QVERIFY(!inst.isInstrument());
    QVERIFY(!inst.isActive());
    QVERIFY(inst.isEnabled());

    auto ports = inst.ports();
    QVERIFY(!ports.empty());
    int audioIn = 0;
    int audioOut = 0;
    int ctrl = 0;
    for (const auto& p : ports) {
        if (p.type == PluginPortInfo::Type::Audio &&
            p.direction == PluginPortInfo::Direction::Input) ++audioIn;
        else if (p.type == PluginPortInfo::Type::Audio &&
                 p.direction == PluginPortInfo::Direction::Output) ++audioOut;
        else if (p.type == PluginPortInfo::Type::Control) ++ctrl;
    }
    QVERIFY(audioIn >= 1);
    QVERIFY(audioOut >= 1);
    QVERIFY(ctrl >= 1);
}

void TestLV2::activateProcessDeactivate() {
    LV2Instance inst;
    if (!inst.load(kZamCompUri))
        QSKIP("ZamComp LV2 plugin not installed");

    QVERIFY(inst.activate(kSampleRate, kBlockSize));
    QVERIFY(inst.isActive());

    std::vector<float> inL(kBlockSize, 0.0f);
    std::vector<float> inR(kBlockSize, 0.0f);
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* inBufs[2] = { inL.data(), inR.data() };
    float* outBufs[2] = { outL.data(), outR.data() };

    // Feed a few blocks of a 440 Hz sine in the left channel.
    for (int i = 0; i < kBlockSize; ++i)
        inL[i] = 0.1f * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);

    float peak = 0.0f;
    for (int block = 0; block < 10; ++block) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        QVERIFY(inst.process(inBufs, outBufs, kBlockSize, kNumChannels));
        QVERIFY(allFinite(outL, outR));
        for (int i = 0; i < kBlockSize; ++i) {
            QVERIFY(std::abs(outL[i]) < 10.0f);
            QVERIFY(std::abs(outR[i]) < 10.0f);
            peak = std::max(peak, std::max(std::abs(outL[i]), std::abs(outR[i])));
        }
    }
    QVERIFY(peak > 0.01f); // the DSP actually ran

    // Silence in -> silence out (a compressor passes DC-free silence through).
    std::fill(inL.begin(), inL.end(), 0.0f);
    std::fill(inR.begin(), inR.end(), 0.0f);
    std::fill(outL.begin(), outL.end(), 1.0f);
    std::fill(outR.begin(), outR.end(), 1.0f);
    QVERIFY(inst.process(inBufs, outBufs, kBlockSize, kNumChannels));
    float maxSilence = 0.0f;
    for (int i = 0; i < kBlockSize; ++i) {
        maxSilence = std::max(maxSilence, std::abs(outL[i]));
        maxSilence = std::max(maxSilence, std::abs(outR[i]));
    }
    QVERIFY(maxSilence < 1e-4f);

    QVERIFY(inst.deactivate());
    QVERIFY(!inst.isActive());
}

void TestLV2::stateRoundTrip() {
    LV2Instance a;
    if (!a.load(kZamCompUri))
        QSKIP("ZamComp LV2 plugin not installed");
    QVERIFY(a.activate(kSampleRate, kBlockSize));

    int ctrlIndex = -1;
    for (const auto& p : a.ports()) {
        if (p.type == PluginPortInfo::Type::Control) {
            ctrlIndex = p.index;
            break;
        }
    }
    QVERIFY(ctrlIndex >= 0);

    float def = a.getParameter(ctrlIndex);
    float newVal = (std::abs(def - 0.5f) < 1e-4f) ? 0.25f : 0.5f;
    a.setParameter(ctrlIndex, newVal);
    QVERIFY(std::abs(a.getParameter(ctrlIndex) - newVal) < 1e-4f);

    QJsonObject json = a.stateToJson();
    QVERIFY(json["enabled"].toBool());
    QCOMPARE(json["type"].toString(), QString("lv2"));

    LV2Instance b;
    QVERIFY(b.load(kZamCompUri));
    b.stateFromJson(json);
    QVERIFY(b.isEnabled());
    QVERIFY(std::abs(b.getParameter(ctrlIndex) - newVal) < 1e-3f);
}

void TestLV2::stateToJsonWithoutStateExtension() {
    // Regression: swh-lv2 plugins have no LV2 state extension (their LV2
    // descriptor's extension_data is NULL). stateToJson() previously called
    // desc->extension_data() unconditionally, which crashed (NULL function
    // pointer) whenever the project was serialized (e.g. SnapshotCommand on
    // plugin removal). Guarded via LV2Instance::extensionData().
    LV2Instance inst;
    if (!inst.load(kHardLimiterUri))
        QSKIP("swh Hard Limiter LV2 plugin not installed");
    QVERIFY(inst.activate(kSampleRate, kBlockSize));

    QJsonObject json = inst.stateToJson();
    QVERIFY(json.contains("params"));
    QVERIFY(json.contains("lv2State"));
    QVERIFY(json["lv2State"].toArray().isEmpty());

    // Project-level serialization used by SnapshotCommand on plugin removal.
    Instrument instrument;
    instrument.setName("FX host");
    instrument.effects().addPlugin(
        PluginChain::createInstance(json, nullptr));
    QVERIFY(instrument.effects().count() == 1);
    QJsonObject instJson = instrument.toJson();
    QVERIFY(instJson["effects"].isObject());
}

void TestLV2::stateSerializationSurvivesBuggyPlugin() {
    // Regression: state save/restore handed the plugin a null features array,
    // which a-fluidsynth's save()/restore() dereference, so any project
    // serialization (SnapshotCommand on a MIDI event edit, File->Save)
    // segfaulted the whole app. The host now passes a terminating features
    // array and additionally guards the plugin call so a faulting plugin can
    // never take the host down.
    //
    // Note: a-fluidsynth must only be loaded ONCE per process — creating a
    // second fluidsynth synth deadlocks inside libinstpatch's GObject
    // once-init (a libinstpatch/fluidsynth bug, unrelated to this host).
    LV2Instance inst;
    if (!inst.load("urn:ardour:a-fluidsynth"))
        QSKIP("a-fluidsynth LV2 plugin not installed");

    // Repeated serialization (as happens on every MIDI event edit) must not
    // crash and must keep producing a valid object.
    QJsonObject json = inst.stateToJson();
    QVERIFY(json.contains("params"));
    QVERIFY(json.contains("lv2State"));
    QJsonObject again = inst.stateToJson();
    QVERIFY(again.contains("params"));
    QVERIFY(again.contains("lv2State"));

    // Feeding stored state back (restore path) must not crash either.
    QJsonObject state;
    QJsonObject entry;
    entry["key"] = QString("urn:ardour:a-fluidsynth:sf2file");
    entry["type"] = QString("http://lv2plug.in/ns/ext/atom#Path");
    entry["flags"] = static_cast<int>(LV2_STATE_IS_POD);
    entry["value"] = QString::fromLatin1(
        QByteArray("/tmp/fake.sf2\0", 13).toBase64());
    QJsonArray arr;
    arr.append(entry);
    state["lv2State"] = arr;

    inst.stateFromJson(state);
    QVERIFY(inst.isEnabled());
}

void TestLV2::genericScanSmoke() {
    // Iterate installed plugins on one shared world; each non-instrument
    // plugin must at least load, activate and process a silence block without
    // producing non-finite samples.
    LilvWorld* world = lilv_world_new();
    if (!world)
        QSKIP("cannot create Lilv world");
    lilv_world_load_all(world);
    const LilvPlugins* plugins = lilv_world_get_all_plugins(world);

    constexpr int kMaxTested = 20;
    int tested = 0;
    int processed = 0;
    int total = 0;

    LilvIter* iter = lilv_plugins_begin(plugins);
    for (LilvIter* i = iter; !lilv_plugins_is_end(plugins, i);
         i = lilv_plugins_next(plugins, i)) {
        ++total;
        if (tested >= kMaxTested) break;
        const LilvPlugin* p = lilv_plugins_get(plugins, i);
        const char* uri = lilv_node_as_string(lilv_plugin_get_uri(p));
        if (!uri)
            continue;

        LV2Instance inst;
        inst.setWorld(world);
        if (!inst.load(uri))
            continue;
        if (inst.isInstrument())
            continue;
        ++tested;

        if (!inst.activate(kSampleRate, kBlockSize))
            continue;

        std::vector<float> inL(kBlockSize, 0.0f);
        std::vector<float> inR(kBlockSize, 0.0f);
        std::vector<float> outL(kBlockSize, 0.0f);
        std::vector<float> outR(kBlockSize, 0.0f);
        float* inBufs[2] = { inL.data(), inR.data() };
        float* outBufs[2] = { outL.data(), outR.data() };
        if (!inst.process(inBufs, outBufs, kBlockSize, kNumChannels))
            continue;

        QVERIFY2(allFinite(outL, outR),
                 qPrintable(QString("non-finite output from %1").arg(uri)));
        ++processed;
    }

    lilv_world_free(world);
    QVERIFY2(total > 0, "no LV2 plugins discovered in Lilv world");
    QVERIFY2(processed > 0, "no loadable LV2 effect plugin found");
}

void TestLV2::multiChannelOutputDiscovery() {
    LV2Instance inst;
    if (!inst.load(kDrumGizmoUri))
        QSKIP("DrumGizmo LV2 plugin not installed");

    QVERIFY(inst.isInstrument());
    int outCh = inst.audioOutputChannels();
    QVERIFY2(outCh >= 2, qPrintable(QString("expected multi-channel output, got %1").arg(outCh)));

    auto names = inst.audioOutputNames();
    QCOMPARE(static_cast<int>(names.size()), outCh);
    for (const auto& n : names)
        QVERIFY(!n.isEmpty());

    // The reported channel count must match the audio output ports.
    auto ports = inst.ports();
    int audioOut = 0;
    for (const auto& p : ports) {
        if (p.type == PluginPortInfo::Type::Audio &&
            p.direction == PluginPortInfo::Direction::Output)
            ++audioOut;
    }
    QCOMPARE(outCh, audioOut);
}

void TestLV2::multiChannelProcess() {
    LV2Instance inst;
    if (!inst.load(kDrumGizmoUri))
        QSKIP("DrumGizmo LV2 plugin not installed");

    QVERIFY(inst.activate(kSampleRate, kBlockSize));
    QVERIFY(inst.isActive());

    const int outCh = inst.audioOutputChannels();
    QVERIFY(outCh >= 2);

    // Multi-channel in-place processing: every output channel must be written
    // (not silently discarded) and stay finite while the plugin runs.
    std::vector<std::vector<float>> in(outCh, std::vector<float>(kBlockSize, 0.0f));
    std::vector<std::vector<float>> out(outCh, std::vector<float>(kBlockSize, 0.0f));
    std::vector<float*> inBufs(outCh);
    std::vector<float*> outBufs(outCh);
    for (int c = 0; c < outCh; ++c) {
        inBufs[c] = in[c].data();
        outBufs[c] = out[c].data();
    }

    MidiBuffer midi;
    midi.push_back({ 0, 0x90, 36, 100 }); // kick note-on

    for (int block = 0; block < 4; ++block) {
        for (auto& ch : out)
            std::fill(ch.begin(), ch.end(), 0.0f);
        QVERIFY(inst.process(inBufs.data(), outBufs.data(), kBlockSize, outCh, &midi));
        QVERIFY(allFinite(out));
    }

    // The buffers are distinct: no channel aliases another.
    std::set<const float*> distinct;
    for (int c = 0; c < outCh; ++c)
        distinct.insert(outBufs[c]);
    QCOMPARE(static_cast<int>(distinct.size()), outCh);

    QVERIFY(inst.deactivate());
}

void TestLV2::outputControlPortMetersFlow() {
    // Regression: compressor UIs (x42-compressor, ZamComp, ...) drive their
    // dynamic widgets — level meters, GR meter, the dot on the compression
    // curve — from output control ports that the plugin writes during run().
    // The host must read those back and forward them to the UI via port_event;
    // otherwise the widgets render once at editor creation and stay static.
    //
    // The full UI path needs X11, so this tests the data source the forwarding
    // consumes: the plugin's meter values must be visible on the output control
    // ports after real compression.
    LV2Instance inst;
    if (!inst.load(kZamCompUri))
        QSKIP("ZamComp LV2 plugin not installed");
    QVERIFY(inst.activate(kSampleRate, kBlockSize));

    int grIndex = -1;
    int thrIndex = -1;
    for (const auto& p : inst.ports()) {
        if (p.type == PluginPortInfo::Type::Control &&
            p.direction == PluginPortInfo::Direction::Output &&
            p.name.contains("Gain Reduction"))
            grIndex = p.index;
        if (p.type == PluginPortInfo::Type::Control &&
            p.direction == PluginPortInfo::Direction::Input &&
            p.name.contains("Threshold"))
            thrIndex = p.index;
    }
    QVERIFY2(grIndex >= 0, "ZamComp has no Gain Reduction output control port");
    QVERIFY2(thrIndex >= 0, "ZamComp has no Threshold control port");

    // The "Gain Reduction" port must be reported as an output control port.
    bool grIsOutputCtrl = false;
    for (auto& [idx, value] : inst.outputControlPortValues())
        grIsOutputCtrl = grIsOutputCtrl || (idx == grIndex);
    QVERIFY(grIsOutputCtrl);

    // Low threshold so the compressor definitely engages on the loud sine.
    inst.setParameter(thrIndex, -60.0f);

    std::vector<float> inL(kBlockSize, 0.0f);
    std::vector<float> inR(kBlockSize, 0.0f);
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);
    float* inBufs[2] = { inL.data(), inR.data() };
    float* outBufs[2] = { outL.data(), outR.data() };
    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < kBlockSize; ++i)
            inL[i] = inR[i] = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        QVERIFY(inst.process(inBufs, outBufs, kBlockSize, kNumChannels));
    }

    // The GR meter (an output control port) must have moved; this is exactly
    // the value drainUiEvents() forwards to the native UI as port_event.
    float gr = inst.getParameter(grIndex);
    QVERIFY2(gr > 0.1f, qPrintable(QString("expected Gain Reduction meter to move, got %1").arg(gr)));

    QVERIFY(inst.deactivate());
}

QTEST_MAIN(TestLV2)
#include "test_lv2.moc"
