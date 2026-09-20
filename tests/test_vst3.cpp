#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <cstring>
#include <string>

#include "plugin/VST3Scan.h"
#include "plugin/Vst3ModuleInfo.h"
#include "plugin/PluginManager.h"

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

class Vst3ScanTest : public QObject {
    Q_OBJECT
private slots:
    void findComponentUIDUsesClassInfoWithoutInstantiating();
    void findComponentUIDMatchesOnlyComponentCategory();
    void instrumentDetectionUsesSubCategories();
    void subCategoriesContainRequiredTokenMatch();

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
