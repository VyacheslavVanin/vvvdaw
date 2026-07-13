#pragma once
#include <QString>
#include <vector>
#include <set>
#include <QJsonObject>

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

    static std::vector<QString> defaultScanPaths();

private:
    std::vector<PluginInfo> m_plugins;
    QString cachePath() const;
};
