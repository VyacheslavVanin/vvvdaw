#pragma once
#include <QString>
#include <vector>
#include <set>
#include <QJsonObject>
#include <lilv/lilv.h>

struct PluginInfo {
    QString name;
    QString vendor;
    QString path;
    QString pluginId;
    QString category;
    QString type;
};

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    void scanDirectories(const std::vector<QString>& directories);
    void loadCache();
    void saveCache();

    const std::vector<PluginInfo>& plugins() const;
    LilvWorld* lilvWorld() const { return m_lilvWorld; }

    static std::vector<QString> defaultScanPaths();

private:
    void scanLV2();
    std::vector<PluginInfo> m_plugins;
    LilvWorld* m_lilvWorld = nullptr;
    QString cachePath() const;
};
