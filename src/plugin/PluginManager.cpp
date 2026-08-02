#include "PluginManager.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <lilv/lilv.h>
#include <lv2/core/lv2.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <dlfcn.h>
#include <cstring>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace Steinberg;

namespace {

using namespace Steinberg::Vst;

// ABI-independent UID discovery: scans the .so for a 16-byte chunk that
// createInstance accepts as an IComponent. Used instead of getClassInfo*
// because the installed plugins were built against older VST3 SDKs whose
// PClassInfo layout mismatches the current headers (getClassInfo crashes).
bool findVst3AudioProcessorUID(IPluginFactory* factory, const std::string& soPath, TUID outUID) {
    memset(outUID, 0, 16);
    std::ifstream file(soPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    auto sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buf(sz);
    file.read(reinterpret_cast<char*>(buf.data()), sz);

    const char compIID[] = "\xE8\x31\xFF\x31\xF2\xD5\x43\x01\x92\x8E\xBB\xEE\x25\x69\x78\x02";

    for (size_t i = 0; i + 16 <= buf.size(); i += 4) {
        bool allZero = true;
        for (int j = 0; j < 16; ++j) if (buf[i+j] != 0) { allZero = false; break; }
        if (allZero) continue;

        TUID tuid;
        memcpy(tuid, buf.data() + i, 16);

        void* obj = nullptr;
        factory->createInstance(tuid, compIID, &obj);
        if (obj) {
            memcpy(outUID, tuid, 16);
            IPluginBase* base = nullptr;
            ((FUnknown*)obj)->queryInterface(IPluginBase::iid, (void**)&base);
            if (base) base->release();
            return true;
        }
    }
    return false;
}

// Detects whether a VST3 bundle contains an instrument (a component with a
// MIDI/event input bus) without relying on getClassInfo (ABI hazard).
bool vst3BundleHasEventInput(const std::string& soPath) {
    void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) return false;
    using GetFactoryFunc = Steinberg::IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(handle, "GetPluginFactory"));
    IPluginFactory* factory = getFactory ? getFactory() : nullptr;
    if (!factory) {
        dlclose(handle);
        return false;
    }

    bool isInstrument = false;
    TUID uid;
    if (findVst3AudioProcessorUID(factory, soPath, uid)) {
        IComponent* comp = nullptr;
        factory->createInstance(uid, IComponent::iid, (void**)&comp);
        if (comp) {
            Steinberg::Vst::HostApplication hostApp;
            comp->initialize(&hostApp);
            isInstrument = comp->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput) > 0;
            comp->terminate();
            comp->release();
        }
    }
    dlclose(handle);
    return isInstrument;
}

} // namespace

PluginManager::PluginManager() {
    m_lilvWorld = lilv_world_new();
    lilv_world_load_all(m_lilvWorld);
    loadCache();
}

PluginManager::~PluginManager() {
    if (m_lilvWorld) {
        lilv_world_free(m_lilvWorld);
        m_lilvWorld = nullptr;
    }
}

void PluginManager::scanDirectories(const std::vector<QString>& directories) {
    std::set<QString> knownPaths;
    for (auto& pi : m_plugins)
        if (pi.type == "vst3")
            knownPaths.insert(pi.path);

    for (auto& dir : directories) {
        namespace fs = std::filesystem;
        fs::path fsDir(dir.toStdString());
        if (!fs::exists(fsDir)) continue;

        for (auto& entry : fs::directory_iterator(fsDir)) {
            if (!entry.is_directory()) continue;
            if (entry.path().extension() != ".vst3") continue;

            QString bundlePath = QString::fromStdString(entry.path().string());
            if (knownPaths.contains(bundlePath)) continue;

            fs::path soPath;
            for (auto& sub : fs::recursive_directory_iterator(entry.path())) {
                if (sub.path().extension() == ".so") {
                    soPath = sub.path();
                    break;
                }
            }
            if (soPath.empty()) continue;

            void* handle = dlopen(soPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) continue;

            using GetFactoryFunc = Steinberg::IPluginFactory* (*)();
            auto getFactory = reinterpret_cast<GetFactoryFunc>(
                dlsym(handle, "GetPluginFactory"));
            Steinberg::IPluginFactory* factory = getFactory ? getFactory() : nullptr;
            dlclose(handle);
            if (!factory) continue;

            bool isInstrument = vst3BundleHasEventInput(soPath.string());

            std::string stem = entry.path().stem().string();
            PluginInfo pi;
            pi.name = QString::fromStdString(stem);
            pi.vendor = QString();
            pi.path = bundlePath;
            pi.pluginId = QString::fromStdString(stem);
            pi.category = isInstrument ? "Instrument" : QString::fromUtf8(kVstAudioEffectClass);
            pi.type = "vst3";
            pi.isInstrument = isInstrument;
            m_plugins.push_back(pi);
        }
    }
    scanLV2();
    saveCache();
}

void PluginManager::scanLV2() {
    if (!m_lilvWorld) return;

    std::set<QString> knownLV2;
    for (auto& pi : m_plugins)
        if (pi.type == "lv2")
            knownLV2.insert(pi.pluginId);

    const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
    LILV_FOREACH(plugins, it, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, it);

        const LilvNode* uriNode = lilv_plugin_get_uri(p);
        QString uri = QString::fromUtf8(lilv_node_as_string(uriNode));
        if (knownLV2.contains(uri)) continue;

        const LilvNode* nameNode = lilv_plugin_get_name(p);
        QString name = nameNode ? QString::fromUtf8(lilv_node_as_string(nameNode)) : uri;

        const LilvNode* authorNode = lilv_plugin_get_author_name(p);
        QString vendor = authorNode ? QString::fromUtf8(lilv_node_as_string(authorNode)) : QString();

        LilvNode* instrumentClass = lilv_new_uri(m_lilvWorld, LV2_CORE__InstrumentPlugin);
        const LilvPluginClass* pluginClass = lilv_plugin_get_class(p);
        const LilvNode* classUri = pluginClass ? lilv_plugin_class_get_uri(pluginClass) : nullptr;
        bool isInstrument = classUri && lilv_node_equals(classUri, instrumentClass);
        lilv_node_free(instrumentClass);

        PluginInfo pi;
        pi.name = name;
        pi.vendor = vendor;
        pi.path = uri;
        pi.pluginId = uri;
        pi.category = isInstrument ? "Instrument" : QString::fromUtf8(LV2_CORE__Plugin);
        pi.type = "lv2";
        pi.isInstrument = isInstrument;
        m_plugins.push_back(pi);
    }
}

void PluginManager::loadCache() {
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    if (root["version"].toInt(0) != kCacheVersion)
        return; // stale cache -> force a rescan

    m_plugins.clear();
    QJsonArray arr = root["plugins"].toArray();
    for (auto v : arr) {
        QJsonObject obj = v.toObject();
        PluginInfo pi;
        pi.name = obj["name"].toString();
        pi.vendor = obj["vendor"].toString();
        pi.path = obj["path"].toString();
        pi.pluginId = obj["pluginId"].toString();
        pi.category = obj["category"].toString();
        pi.type = obj["type"].toString();
        pi.isInstrument = obj["isInstrument"].toBool(false);
        m_plugins.push_back(pi);
    }
}

void PluginManager::saveCache() {
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonObject root;
    root["version"] = kCacheVersion;
    QJsonArray arr;
    for (auto& pi : m_plugins) {
        QJsonObject obj;
        obj["name"] = pi.name;
        obj["vendor"] = pi.vendor;
        obj["path"] = pi.path;
        obj["pluginId"] = pi.pluginId;
        obj["category"] = pi.category;
        obj["type"] = pi.type;
        obj["isInstrument"] = pi.isInstrument;
        arr.append(obj);
    }
    root["plugins"] = arr;
    file.write(QJsonDocument(root).toJson());
}

const std::vector<PluginInfo>& PluginManager::plugins() const {
    return m_plugins;
}

QString PluginManager::cachePath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + "/plugin_cache.json";
}

std::vector<QString> PluginManager::defaultScanPaths() {
    std::vector<QString> paths;
    QString home = QDir::homePath();
    paths.push_back(home + "/.vst3");
    paths.push_back("/usr/lib/vst3");
    paths.push_back("/usr/local/lib/vst3");
    paths.push_back(home + "/.lv2");
    paths.push_back("/usr/lib/lv2");
    paths.push_back("/usr/local/lib/lv2");
    return paths;
}
