#include "PluginManager.h"
#include "VST3Scan.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <lilv/lilv.h>
#include <lv2/core/lv2.h>
#include <filesystem>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace Steinberg;

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

        std::vector<fs::path> bundles;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(fsDir, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            const auto& entry = *it;
            if (!entry.is_directory()) continue;
            if (entry.path().extension() != ".vst3") continue;
            bundles.push_back(entry.path());
            it.disable_recursion_pending();
        }

        for (auto& bundle : bundles) {
            QString bundlePath = QString::fromStdString(bundle.string());
            if (knownPaths.contains(bundlePath)) continue;

            fs::path soPath;
            for (auto& sub : fs::recursive_directory_iterator(bundle)) {
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

            bool isInstrument = VST3Scan::bundleHasEventInput(soPath.string());

            std::string stem = bundle.stem().string();
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
