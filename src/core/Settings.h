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
    int midiInputDeviceId;
    int midiTransportControlType;
    int midiTransportPlayControl;
    int midiTransportRecordControl;
    int midiTransportStopControl;
    int streamingThresholdSec;
    bool mouseWheelScroll;
    int pluginKnobsPerRow;

    QString lastProjectPath;
    std::vector<QString> pluginScanPaths;

    // Main-window panel layout, restored on the next launch so the user does
    // not have to re-arrange the panels when reopening a project.
    bool busPanelVisible;
    int busPanelHeight;
    bool instrumentPanelVisible;
    int instrumentPanelHeight;

    // Main-window size, restored on the next launch.
    int mainWindowWidth;
    int mainWindowHeight;

private:
    QString configFilePath() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    static QString s_configDirOverride;

    std::vector<RecentProject> m_recentProjects;
};
