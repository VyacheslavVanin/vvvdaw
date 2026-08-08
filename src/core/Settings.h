#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include <cstdint>

class Settings {
public:
    static constexpr int MaxRecentProjects = 10;

    struct RecentProject {
        QString path;
        qint64 lastOpenedMs = 0;
    };

    Settings();

    void load();
    void save();

    // A recently opened/saved project, most recent first (capped at 10).
    const std::vector<RecentProject>& recentProjects() const { return m_recentProjects; }
    void addRecentProject(const QString& path);
    void removeRecentProject(const QString& path);

    // Test seam: redirect the config directory (otherwise QStandardPaths).
    static void setConfigDirOverride(const QString& dir);

    int sampleRate;
    int bufferSize;
    int inputDeviceId;
    int outputDeviceId;
    int inputChannel;
    int outputChannel;
    int streamingThresholdSec;
    bool mouseWheelScroll;
    int pluginKnobsPerRow;

    QString lastProjectPath;
    std::vector<QString> pluginScanPaths;

private:
    QString configFilePath() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    static QString s_configDirOverride;

    std::vector<RecentProject> m_recentProjects;
};
