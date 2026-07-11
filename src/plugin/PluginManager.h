#pragma once
#include <QString>
#include <vector>
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
    void scanLV2();
    void ensureLV2Loaded();
    void loadCache();
    void saveCache();

    const std::vector<PluginInfo>& plugins() const;
    LilvWorld* lilvWorld() { return m_lilvWorld; }
    const LilvWorld* lilvWorld() const { return m_lilvWorld; }
    const LilvPlugin* findLV2Plugin(const QString& uri) const;

    static std::vector<QString> defaultScanPaths();

private:
    std::vector<PluginInfo> m_plugins;
    LilvWorld* m_lilvWorld = nullptr;
    QString cachePath() const;
};
