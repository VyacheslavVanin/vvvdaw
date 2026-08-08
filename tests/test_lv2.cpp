#include <QTest>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <vector>

#include "plugin/LV2Instance.h"
#include "plugin/SigGuard.h"

// Smoke / integration tests for the LV2 backend. These exercise the real
// Lilv world and installed plugins, so they verify the load/activate/process/
// state contracts without crashing rather than DSP correctness. Tests that
// need a specific plugin skip gracefully when it is not installed.

namespace {

constexpr const char* kZamCompUri = "urn:zamaudio:ZamComp";
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

} // namespace

class TestLV2 : public QObject {
    Q_OBJECT
private slots:
    void sigGuardRecovers();
    void loadKnownPlugin();
    void activateProcessDeactivate();
    void stateRoundTrip();
    void genericScanSmoke();
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

QTEST_MAIN(TestLV2)
#include "test_lv2.moc"
