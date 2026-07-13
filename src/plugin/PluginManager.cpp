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

PluginManager::PluginManager() {
    loadCache();
}

PluginManager::~PluginManager() {
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
            bool hasFactory = (getFactory && getFactory());
            dlclose(handle);
            if (!hasFactory) continue;

            std::string stem = entry.path().stem().string();
            PluginInfo pi;
            pi.name = QString::fromStdString(stem);
            pi.vendor = QString();
            pi.path = bundlePath;
            pi.pluginId = QString::fromStdString(stem);
            pi.category = QString::fromUtf8(kVstAudioEffectClass);
            pi.type = "vst3";
            m_plugins.push_back(pi);
        }
    }
    saveCache();
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
