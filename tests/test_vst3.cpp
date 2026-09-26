#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "plugin/PluginAudioUtils.h"
#include "plugin/SigGuard.h"
#include "plugin/VST3Instance.h"
#include "plugin/VST3Scan.h"
#include "plugin/Vst3ModuleInfo.h"
#include "plugin/PluginManager.h"
#include <public.sdk/source/vst/vstaudioeffect.h>

// A factory whose createInstance() must never be called during UID discovery.
// Before the fix, VST3Scan::findComponentUID() ran a binary memory scan that
// called createInstance() once per 4-byte window; for a JUCE 8 plugin every
// call spun up a ScopedRunLoop/message thread and the scan deadlocked. This
// mock counts the calls so the regression is caught deterministically without
// any real plugin.
class MockFactory : public Steinberg::IPluginFactory2 {
public:
    int createInstanceCalls = 0;
    Steinberg::TUID componentCID{};
    std::string subCategories = "Fx";

    Steinberg::tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override {
        if (info) std::memset(info, 0, sizeof(*info));
        return Steinberg::kResultOk;
    }

    Steinberg::int32 PLUGIN_API countClasses() override { return 1; }

    Steinberg::tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                               Steinberg::PClassInfo* info) override {
        if (index != 0 || !info) return Steinberg::kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, componentCID, 16);
        info->cardinality = Steinberg::PClassInfo::kManyInstances;
        std::strncpy(info->category, kVstAudioEffectClass, sizeof(info->category) - 1);
        std::strncpy(info->name, "Mock", sizeof(info->name) - 1);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API getClassInfo2(Steinberg::int32 index,
                                                Steinberg::PClassInfo2* info) override {
        if (index != 0 || !info) return Steinberg::kInvalidArgument;
        std::memset(info, 0, sizeof(*info));
        std::memcpy(info->cid, componentCID, 16);
        info->cardinality = Steinberg::PClassInfo::kManyInstances;
        std::strncpy(info->category, kVstAudioEffectClass, sizeof(info->category) - 1);
        std::strncpy(info->subCategories, subCategories.c_str(),
                     sizeof(info->subCategories) - 1);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API createInstance(Steinberg::FIDString, Steinberg::FIDString,
                                                 void** obj) override {
        ++createInstanceCalls;
        if (obj) *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                 void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPluginFactory::iid)) {
            *obj = static_cast<Steinberg::IPluginFactory*>(this);
            return Steinberg::kResultTrue;
        }
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPluginFactory2::iid)) {
            *obj = static_cast<Steinberg::IPluginFactory2*>(this);
            return Steinberg::kResultTrue;
        }
        *obj = nullptr;
        return Steinberg::kResultFalse;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 0; }
};

// Concrete VST3 component/processor (SDK AudioEffect) that records the order of
// the activation calls. Used to pin down the JUCE bus-map requirement:
// setupProcessing() must run before activateBus().
class RecordingEffect : public Steinberg::Vst::AudioEffect {
public:
    std::vector<std::string> calls;

    Steinberg::int32 PLUGIN_API getBusCount(Steinberg::Vst::MediaType type,
                                            Steinberg::Vst::BusDirection dir) override {
        if (type != Steinberg::Vst::kAudio) return 0;
        return dir == Steinberg::Vst::kInput ? 1 : 1;
    }

    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override {
        calls.push_back("setupProcessing");
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override {
        calls.push_back("setProcessing");
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override {
        calls.push_back("setActive");
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType,
                                              Steinberg::Vst::BusDirection,
                                              Steinberg::int32, Steinberg::TBool) override {
        calls.push_back("activateBus");
        return Steinberg::kResultTrue;
    }
};

class Vst3ScanTest : public QObject {
    Q_OBJECT
private slots:
    void findComponentUIDUsesClassInfoWithoutInstantiating();
    void findComponentUIDMatchesOnlyComponentCategory();
    void instrumentDetectionUsesSubCategories();
    void subCategoriesContainRequiredTokenMatch();

    void activationReappliesBusActivationAfterSetupProcessing();
    void expandChannelsRepeatsHostChannels();
    void reduceChannelsFoldsPluginOutput();

    void monoFoldChannelPointerOutlivesCall();
    void expandMonoHostToStereoBusUsesPersistentStorage();
    void stereoBusMapsHostBuffersDirectly();
    void monoInputPluginOnStereoBusDoesNotCrash();

    void moduleInfoParsesStandardJson();
    void moduleInfoParsesTrailingCommas();
    void moduleInfoKeepsCommasInsideStrings();
    void moduleInfoPicksAudioModuleClass();
    void moduleInfoRejectsInvalidDocuments();
    void scanUsesModuleInfoMetadata();
};

void Vst3ScanTest::findComponentUIDUsesClassInfoWithoutInstantiating() {
    MockFactory factory;
    const Steinberg::TUID cid = {'T', 'e', 's', 't', 'C', 'o', 'm', 'p',
                                 'o', 'n', 'e', 'n', 't', 'C', 'I', 'D'};
    std::memcpy(factory.componentCID, cid, 16);

    Steinberg::TUID out{};
    // A non-existent .so path proves the class-info path returned before the
    // binary scan (which would fail to open the file) ever ran.
    QVERIFY(VST3Scan::findComponentUID(&factory, "/nonexistent/plugin.so", out));
    QCOMPARE(factory.createInstanceCalls, 0);
    QVERIFY(std::memcmp(out, cid, 16) == 0);
}

void Vst3ScanTest::findComponentUIDMatchesOnlyComponentCategory() {
    // A factory with no "Audio Module Class" must not yield a UID from the
    // class-info path; the caller then falls back to the scan.
    class NoComponentFactory : public MockFactory {
    public:
        Steinberg::tresult PLUGIN_API getClassInfo(Steinberg::int32,
                                                   Steinberg::PClassInfo* info) override {
            if (info) {
                std::memset(info, 0, sizeof(*info));
                std::strncpy(info->category, "Component Controller Class",
                             sizeof(info->category) - 1);
            }
            return Steinberg::kResultOk;
        }
    };
    NoComponentFactory factory;
    Steinberg::TUID out{};
    QVERIFY(!VST3Scan::findComponentUIDByClassInfo(&factory, out));
    QCOMPARE(factory.createInstanceCalls, 0);
}

void Vst3ScanTest::instrumentDetectionUsesSubCategories() {
    MockFactory factory;
    Steinberg::TUID cid = {'I', 'n', 's', 't', 'r', 'u', 'm', 'e',
                           'n', 't', 'C', 'I', 'D', '0', '0', '0'};
    std::memcpy(factory.componentCID, cid, 16);

    factory.subCategories = "Fx";
    QVERIFY(!VST3Scan::componentIsInstrument(&factory, cid));

    factory.subCategories = "Fx|Instrument";
    QVERIFY(VST3Scan::componentIsInstrument(&factory, cid));

    factory.subCategories = "Instrument|Synth";
    QVERIFY(VST3Scan::componentIsInstrument(&factory, cid));

    QCOMPARE(factory.createInstanceCalls, 0);
}

void Vst3ScanTest::subCategoriesContainRequiredTokenMatch() {
    QVERIFY(VST3Scan::subCategoriesContain("Fx|Instrument", "Instrument"));
    QVERIFY(VST3Scan::subCategoriesContain("Instrument", "Instrument"));
    QVERIFY(!VST3Scan::subCategoriesContain("Fx", "Instrument"));
    // "Instruments" must not match the exact "Instrument" token.
    QVERIFY(!VST3Scan::subCategoriesContain("Fx|Instruments", "Instrument"));
}

void Vst3ScanTest::activationReappliesBusActivationAfterSetupProcessing() {
    RecordingEffect effect;
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = 256;
    setup.sampleRate = 48000.0;

    QVERIFY(VST3Instance::activateComponent(&effect, &effect, setup));

    const auto firstOf = [&](const char* name) {
        const auto it = std::find(effect.calls.begin(), effect.calls.end(), name);
        if (it == effect.calls.end()) return -1;
        return static_cast<int>(std::distance(effect.calls.begin(), it));
    };

    const int setupIdx = firstOf("setupProcessing");
    const int busIdx = firstOf("activateBus");
    const int activeIdx = firstOf("setActive");
    const int procIdx = firstOf("setProcessing");

    QVERIFY(setupIdx >= 0);
    // The regression: activateBus() before setupProcessing() is dropped by
    // JUCE, leaving every bus host-inactive and the plugin silent.
    QVERIFY2(busIdx > setupIdx, "activateBus must run after setupProcessing");
    QVERIFY(activeIdx > busIdx);
    QVERIFY(procIdx > activeIdx);
}

void Vst3ScanTest::expandChannelsRepeatsHostChannels() {
    const int N = 8;
    std::vector<float> mono(N, 0.5f);
    const float* src[1] = {mono.data()};
    std::vector<float> l(N, 0.0f), r(N, 0.0f);
    float* dst[2] = {l.data(), r.data()};

    expandChannelsToBus(src, 1, dst, 2, N);

    for (int i = 0; i < N; ++i) {
        QCOMPARE(l[i], 0.5f);
        QCOMPARE(r[i], 0.5f);
    }
}

void Vst3ScanTest::reduceChannelsFoldsPluginOutput() {
    const int N = 4;
    std::vector<float> l(N, 1.0f), r(N, 3.0f);
    const float* src[2] = {l.data(), r.data()};
    std::vector<float> out(N, 0.0f);
    float* dst[1] = {out.data()};

    // Stereo plugin output folded to a mono host averages both channels.
    reduceChannelsToHost(src, 2, dst, 1, N);
    for (int i = 0; i < N; ++i) QCOMPARE(out[i], 2.0f);

    // Four plugin channels onto two host channels: round-robin averages.
    std::vector<float> c0(N, 1.0f), c1(N, 2.0f), c2(N, 3.0f), c3(N, 4.0f);
    const float* src4[4] = {c0.data(), c1.data(), c2.data(), c3.data()};
    std::vector<float> o0(N), o1(N);
    float* dst2[2] = {o0.data(), o1.data()};
    reduceChannelsToHost(src4, 4, dst2, 2, N);
    for (int i = 0; i < N; ++i) {
        QCOMPARE(o0[i], 2.0f); // (c0 + c2) / 2
        QCOMPARE(o1[i], 3.0f); // (c1 + c3) / 2
    }
}

namespace {

// Assembles an InputBusLayout whose buffers outlive the buildInputBuses() call,
// mirroring how VST3Instance::process() owns its scratch and bus metadata.
struct InputBusLayoutFixture {
    std::vector<Steinberg::int32> busChannels;
    std::vector<bool> busIsSidechain;
    std::vector<int> busSidechainOffset;
    std::vector<float*> sidechainPtrs;
    std::vector<float*> monoFoldPtrs;
    std::vector<float*> expandPtrs;
    std::vector<float> monoScratch;

    VST3Instance::InputBusLayout layout() {
        VST3Instance::InputBusLayout l;
        l.busChannels = &busChannels;
        l.busIsSidechain = &busIsSidechain;
        l.busSidechainOffset = &busSidechainOffset;
        l.sidechainPtrs = &sidechainPtrs;
        l.sidechainChannelCount = 0;
        l.monoFoldPtrs = &monoFoldPtrs;
        l.expandPtrs = &expandPtrs;
        l.monoScratch = &monoScratch;
        return l;
    }
};

} // namespace

void Vst3ScanTest::monoFoldChannelPointerOutlivesCall() {
    const int N = 16;
    std::vector<float> left(N, 1.0f), right(N, -2.0f);
    float* in[2] = {left.data(), right.data()};

    InputBusLayoutFixture fx;
    fx.busChannels = {1};
    fx.busIsSidechain = {false};
    fx.busSidechainOffset = {-1};
    fx.monoFoldPtrs.assign(1, nullptr);
    fx.expandPtrs.assign(1, nullptr);
    fx.monoScratch.assign(N, 0.0f);

    std::vector<Steinberg::Vst::AudioBusBuffers> inBuses(1);
    VST3Instance::buildInputBuses(fx.layout(), inBuses.data(), 1, in, N, 2);

    QCOMPARE(inBuses[0].numChannels, 1);
    QVERIFY(inBuses[0].channelBuffers32 != nullptr);
    // Regression: the channel-pointer array must live in caller-owned storage,
    // not in buildInputBuses()'s dead stack frame. ZamEQ2 read inputs[0] from a
    // dangling local array here and faulted.
    QCOMPARE(inBuses[0].channelBuffers32, fx.monoFoldPtrs.data());
    QCOMPARE(inBuses[0].channelBuffers32[0], fx.monoScratch.data());
    for (int i = 0; i < N; ++i)
        QCOMPARE(fx.monoScratch[static_cast<size_t>(i)], -0.5f); // (L+R)/2
}

void Vst3ScanTest::expandMonoHostToStereoBusUsesPersistentStorage() {
    const int N = 8;
    std::vector<float> mono(N, 0.25f), e0(N, 0.0f), e1(N, 0.0f);
    float* in[1] = {mono.data()};

    InputBusLayoutFixture fx;
    fx.busChannels = {2};
    fx.busIsSidechain = {false};
    fx.busSidechainOffset = {-1};
    fx.expandPtrs = {e0.data(), e1.data()};
    fx.monoFoldPtrs.assign(1, nullptr);
    fx.monoScratch.assign(N, 0.0f);

    std::vector<Steinberg::Vst::AudioBusBuffers> inBuses(1);
    VST3Instance::buildInputBuses(fx.layout(), inBuses.data(), 1, in, N, 1);

    QCOMPARE(inBuses[0].numChannels, 2);
    QCOMPARE(inBuses[0].channelBuffers32, fx.expandPtrs.data());
    for (int i = 0; i < N; ++i) {
        QCOMPARE(e0[static_cast<size_t>(i)], 0.25f);
        QCOMPARE(e1[static_cast<size_t>(i)], 0.25f);
    }
}

void Vst3ScanTest::stereoBusMapsHostBuffersDirectly() {
    const int N = 4;
    std::vector<float> l(N, 1.0f), r(N, 2.0f);
    float* in[2] = {l.data(), r.data()};

    InputBusLayoutFixture fx;
    fx.busChannels = {2};
    fx.busIsSidechain = {false};
    fx.busSidechainOffset = {-1};
    fx.monoFoldPtrs.assign(1, nullptr);
    fx.expandPtrs.assign(2, nullptr);
    fx.monoScratch.assign(N, 0.0f);

    std::vector<Steinberg::Vst::AudioBusBuffers> inBuses(1);
    VST3Instance::buildInputBuses(fx.layout(), inBuses.data(), 1, in, N, 2);

    QCOMPARE(inBuses[0].numChannels, 2);
    QCOMPARE(inBuses[0].channelBuffers32, in);
}

void Vst3ScanTest::monoInputPluginOnStereoBusDoesNotCrash() {
    // ZamEQ2 is a 1-in/1-out DPF VST3. On a stereo bus the host folds L/R into
    // the mono input; the fold's channel-pointer array used to point at a dead
    // stack frame, and the plugin faulted reading inputs[0] (the reported SEGV
    // at ZamEQ2.so+0xc6f2). runSigGuarded turns any such fault into a test
    // failure instead of killing the test binary.
    const QString bundle = "/usr/lib/vst3/ZamEQ2.vst3";
    if (!QFile::exists(bundle + "/Contents/x86_64-linux/ZamEQ2.so"))
        QSKIP("ZamEQ2 VST3 plugin not installed");

    VST3Instance inst;
    QVERIFY(inst.load(bundle));

    const int N = 256;
    QVERIFY(inst.activate(48000.0, N));

    std::vector<float> l(N, 0.5f), r(N, -0.5f), ol(N, 0.0f), orr(N, 0.0f);
    float* in[2] = {l.data(), r.data()};
    float* out[2] = {ol.data(), orr.data()};

    // Two calls: the dangling channel-pointer array was rebuilt (and left
    // dangling) on every single process() call.
    bool ok = false;
    const bool survived = runSigGuarded([&] {
        ok = inst.process(in, out, N, 2) && inst.process(in, out, N, 2);
    });
    QVERIFY2(survived, "VST3 process() crashed with a mono-input plugin on a stereo bus");
    QVERIFY(ok);
}

void Vst3ScanTest::moduleInfoParsesStandardJson() {
    const QByteArray json = R"({
        "Name": "TestPlugin",
        "Factory Info": { "Vendor": "TestVendor" },
        "Classes": [
            { "Category": "Audio Module Class", "Sub Categories": [ "Fx" ] }
        ]
    })";

    Vst3ModuleInfo::Meta meta;
    QVERIFY(Vst3ModuleInfo::parse(json, kVstAudioEffectClass, meta));
    QCOMPARE(meta.name, QString("TestPlugin"));
    QCOMPARE(meta.vendor, QString("TestVendor"));
    QVERIFY(!meta.isInstrument);
}

void Vst3ScanTest::moduleInfoParsesTrailingCommas() {
    // The VST3/Steam writer emits trailing commas after the last member of an
    // object and the last element of an array, at any nesting depth.
    const QByteArray json = R"({
        "Name": "Trailing",
        "Factory Info": {
            "Vendor": "Vendor,",
            "Flags": { "Unicode": true, },
        },
        "Classes": [
            {
                "CID": "00000000000000000000000000000001",
                "Category": "Audio Module Class",
                "Sub Categories": [ "Fx", "Instrument", ],
            },
        ],
    })";

    Vst3ModuleInfo::Meta meta;
    QVERIFY(Vst3ModuleInfo::parse(json, kVstAudioEffectClass, meta));
    QCOMPARE(meta.name, QString("Trailing"));
    QCOMPARE(meta.vendor, QString("Vendor,"));
    QVERIFY(meta.isInstrument);
}

void Vst3ScanTest::moduleInfoKeepsCommasInsideStrings() {
    // Commas followed by '}' / ']' inside string literals must not be removed,
    // and escaped quotes must not terminate the string early. The comma after
    // the vendor value is a real trailing comma and must be dropped.
    const QByteArray json = R"({
        "Name": "A,} B,] C\", D",
        "Factory Info": { "Vendor": "V,}", },
        "Classes": [
            { "Category": "Audio Module Class", "Sub Categories": [ "Fx", ], },
        ],
    })";

    Vst3ModuleInfo::Meta meta;
    QVERIFY(Vst3ModuleInfo::parse(json, kVstAudioEffectClass, meta));
    QCOMPARE(meta.name, QString("A,} B,] C\", D"));
    QCOMPARE(meta.vendor, QString("V,}"));
    QVERIFY(!meta.isInstrument);
}

void Vst3ScanTest::moduleInfoPicksAudioModuleClass() {
    // A controller class precedes the component class and must be skipped; its
    // own subcategories must not affect the result.
    const QByteArray withController = R"({
        "Name": "Picked",
        "Classes": [
            { "Category": "Component Controller Class", "Sub Categories": [ "Instrument" ] },
            { "Category": "Audio Module Class", "Sub Categories": [ "Fx" ] }
        ]
    })";
    Vst3ModuleInfo::Meta meta;
    QVERIFY(Vst3ModuleInfo::parse(withController, kVstAudioEffectClass, meta));
    QVERIFY(!meta.isInstrument);

    const QByteArray controllerOnly = R"({
        "Name": "ControllerOnly",
        "Classes": [ { "Category": "Component Controller Class" } ]
    })";
    Vst3ModuleInfo::Meta ignored;
    QVERIFY(!Vst3ModuleInfo::parse(controllerOnly, kVstAudioEffectClass, ignored));
}

void Vst3ScanTest::moduleInfoRejectsInvalidDocuments() {
    Vst3ModuleInfo::Meta meta;
    QVERIFY(!Vst3ModuleInfo::parse("not json at all", kVstAudioEffectClass, meta));
    QVERIFY(!Vst3ModuleInfo::parse("[]", kVstAudioEffectClass, meta));
    QVERIFY(!Vst3ModuleInfo::parse("{}", kVstAudioEffectClass, meta));
    QVERIFY(!Vst3ModuleInfo::parse(R"({ "Classes": [] })", kVstAudioEffectClass, meta));
    // Unbalanced trailing comma outside a string still invalid -> rejected.
    QVERIFY(!Vst3ModuleInfo::parse(R"({ "Name": "x",,})", kVstAudioEffectClass, meta));
}

void Vst3ScanTest::scanUsesModuleInfoMetadata() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString bundle = tmp.filePath("TestBundle.vst3");
    QVERIFY(QDir().mkpath(bundle + "/Contents/Resources"));
    QVERIFY(QDir().mkpath(bundle + "/Contents/x86_64-linux"));
    QFile so(bundle + "/Contents/x86_64-linux/dummy.so");
    QVERIFY(so.open(QIODevice::WriteOnly));
    so.write("x");
    so.close();

    QFile mi(bundle + "/Contents/Resources/moduleinfo.json");
    QVERIFY(mi.open(QIODevice::WriteOnly));
    mi.write(R"({
        "Name": "TestPlugin",
        "Factory Info": { "Vendor": "TestVendor", },
        "Classes": [
            { "Category": "Audio Module Class", "Sub Categories": [ "Fx", "Instrument", ], },
        ],
    })");
    mi.close();

    PluginManager manager;
    manager.scanDirectories({tmp.path()});

    const PluginInfo* found = nullptr;
    for (const auto& pi : manager.plugins())
        if (pi.path == bundle) found = &pi;

    QVERIFY(found);
    QCOMPARE(found->name, QString("TestPlugin"));
    QCOMPARE(found->vendor, QString("TestVendor"));
    QVERIFY(found->isInstrument);
    QCOMPARE(found->type, QString("vst3"));
    QCOMPARE(found->pluginId, QString("TestBundle"));
}

QTEST_MAIN(Vst3ScanTest)
#include "test_vst3.moc"
