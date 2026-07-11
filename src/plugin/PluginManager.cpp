#include "PluginManager.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <filesystem>
#include <dlfcn.h>
#include <cstring>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace Steinberg;

PluginManager::PluginManager()
    : m_lilvWorld(lilv_world_new()) {
    loadCache();
}

PluginManager::~PluginManager() {
    if (m_lilvWorld) lilv_world_free(m_lilvWorld);
}

void PluginManager::scanDirectories(const std::vector<QString>& directories) {
    m_plugins.clear();
    for (auto& dir : directories) {
        namespace fs = std::filesystem;
        fs::path fsDir(dir.toStdString());
        if (!fs::exists(fsDir)) continue;

        for (auto& entry : fs::directory_iterator(fsDir)) {
            if (!entry.is_directory()) continue;
            if (entry.path().extension() != ".vst3") continue;

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
            if (!getFactory) { dlclose(handle); continue; }

            Steinberg::IPluginFactory* rawFactory = getFactory();
            if (!rawFactory) { dlclose(handle); continue; }

            {
                Steinberg::IPtr<Steinberg::IPluginFactory> factory;
                factory = Steinberg::owned(rawFactory);

                for (int32 i = 0; i < factory->countClasses(); ++i) {
                    PClassInfo ci;
                    factory->getClassInfo(i, &ci);
                    if (std::string(ci.category) == kVstAudioEffectClass) {
                        PluginInfo pi;
                        pi.name = QString::fromUtf8(ci.name);
                        pi.vendor = QString();
                        pi.path = QString::fromStdString(entry.path().string());
                        pi.pluginId = QString::fromUtf8(ci.name);
                        pi.category = QString::fromUtf8(ci.category);
                        pi.type = "vst3";
                        m_plugins.push_back(pi);
                    }
                }
            }

            dlclose(handle);
        }
    }
    saveCache();
}

void PluginManager::scanLV2() {
    if (!m_lilvWorld) return;
    lilv_world_load_all(m_lilvWorld);

    const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);
    LILV_FOREACH (plugins, i, plugins) {
        const LilvPlugin* p = lilv_plugins_get(plugins, i);

        LilvNode* nameNode = lilv_plugin_get_name(p);
        if (!nameNode) continue;

        PluginInfo pi;
        pi.name = QString::fromUtf8(lilv_node_as_string(nameNode));
        pi.vendor = QString();
        pi.path = QString();
        pi.pluginId = QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(p)));
        pi.category = QString("LV2 Plugin");
        pi.type = "lv2";

        lilv_node_free(nameNode);
        m_plugins.push_back(pi);
    }
    saveCache();
}

void PluginManager::ensureLV2Loaded() {
    if (!m_lilvWorld) return;
    lilv_world_load_all(m_lilvWorld);
}

const LilvPlugin* PluginManager::findLV2Plugin(const QString& uri) const {
    if (!m_lilvWorld) return nullptr;
    const LilvPlugins* plugins = lilv_world_get_all_plugins(m_lilvWorld);

    LilvNode* pluginUri = lilv_new_uri(m_lilvWorld, uri.toUtf8().constData());
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, pluginUri);
    lilv_node_free(pluginUri);
    return plugin;
}

void PluginManager::loadCache() {
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
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
        m_plugins.push_back(pi);
    }
}

void PluginManager::saveCache() {
    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonObject root;
    QJsonArray arr;
    for (auto& pi : m_plugins) {
        QJsonObject obj;
        obj["name"] = pi.name;
        obj["vendor"] = pi.vendor;
        obj["path"] = pi.path;
        obj["pluginId"] = pi.pluginId;
        obj["category"] = pi.category;
        obj["type"] = pi.type;
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
    return paths;
}
