#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "plugin/PluginAudioUtils.h"
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
