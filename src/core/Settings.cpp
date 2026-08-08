#include "Settings.h"
#include "Constants.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <algorithm>

QString Settings::s_configDirOverride;

void Settings::setConfigDirOverride(const QString& dir) {
    s_configDirOverride = dir;
}

Settings::Settings()
    : sampleRate(vvvdaw::DefaultSampleRate)
    , bufferSize(vvvdaw::DefaultBufferSize)
    , inputDeviceId(-1)
    , outputDeviceId(-1)
    , inputChannel(0)
    , outputChannel(0)
    , streamingThresholdSec(30)
    , mouseWheelScroll(false)
    , pluginKnobsPerRow(3)
{
}

QString Settings::configFilePath() const {
    QString dir = s_configDirOverride.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                      : s_configDirOverride;
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

void Settings::addRecentProject(const QString& path) {
    if (path.isEmpty())
        return;
    m_recentProjects.erase(
        std::remove_if(m_recentProjects.begin(), m_recentProjects.end(),
                       [&](const RecentProject& r) { return r.path == path; }),
        m_recentProjects.end());
    m_recentProjects.insert(m_recentProjects.begin(),
                            RecentProject{path, QDateTime::currentMSecsSinceEpoch()});
    if (static_cast<int>(m_recentProjects.size()) > MaxRecentProjects)
        m_recentProjects.resize(MaxRecentProjects);
}

void Settings::removeRecentProject(const QString& path) {
    m_recentProjects.erase(
        std::remove_if(m_recentProjects.begin(), m_recentProjects.end(),
                       [&](const RecentProject& r) { return r.path == path; }),
        m_recentProjects.end());
}

void Settings::load() {
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject())
        fromJson(doc.object());
}

void Settings::save() {
    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonDocument doc(toJson());
    file.write(doc.toJson());
}

QJsonObject Settings::toJson() const {
    QJsonObject obj;
    obj["sampleRate"] = sampleRate;
    obj["bufferSize"] = bufferSize;
    obj["inputDeviceId"] = inputDeviceId;
    obj["outputDeviceId"] = outputDeviceId;
    obj["inputChannel"] = inputChannel;
    obj["outputChannel"] = outputChannel;
    obj["lastProjectPath"] = lastProjectPath;
    obj["streamingThresholdSec"] = streamingThresholdSec;
    obj["mouseWheelScroll"] = mouseWheelScroll;
    obj["pluginKnobsPerRow"] = pluginKnobsPerRow;

    QJsonArray pathsArr;
    for (const auto& path : pluginScanPaths)
        pathsArr.append(path);
    obj["pluginScanPaths"] = pathsArr;

    QJsonArray recentArr;
    for (const auto& r : m_recentProjects) {
        QJsonObject rObj;
        rObj["path"] = r.path;
        rObj["lastOpenedMs"] = r.lastOpenedMs;
        recentArr.append(rObj);
    }
    obj["recentProjects"] = recentArr;

    return obj;
}

void Settings::fromJson(const QJsonObject& obj) {
    if (obj.contains("sampleRate")) sampleRate = obj["sampleRate"].toInt();
    if (obj.contains("bufferSize")) bufferSize = obj["bufferSize"].toInt();
    if (obj.contains("inputDeviceId")) inputDeviceId = obj["inputDeviceId"].toInt();
    if (obj.contains("outputDeviceId")) outputDeviceId = obj["outputDeviceId"].toInt();
    if (obj.contains("inputChannel")) inputChannel = obj["inputChannel"].toInt();
    if (obj.contains("outputChannel")) outputChannel = obj["outputChannel"].toInt();
    if (obj.contains("lastProjectPath")) lastProjectPath = obj["lastProjectPath"].toString();
    if (obj.contains("streamingThresholdSec")) streamingThresholdSec = obj["streamingThresholdSec"].toInt(30);
    if (obj.contains("mouseWheelScroll")) mouseWheelScroll = obj["mouseWheelScroll"].toBool(false);
    if (obj.contains("pluginKnobsPerRow")) pluginKnobsPerRow = obj["pluginKnobsPerRow"].toInt(3);
    if (obj.contains("pluginScanPaths")) {
        pluginScanPaths.clear();
        QJsonArray arr = obj["pluginScanPaths"].toArray();
        for (const auto& v : arr)
            pluginScanPaths.push_back(v.toString());
    }

    m_recentProjects.clear();
    QJsonArray recentArr = obj["recentProjects"].toArray();
    for (const auto& v : recentArr) {
        QJsonObject rObj = v.toObject();
        RecentProject r;
        r.path = rObj["path"].toString();
        r.lastOpenedMs = rObj["lastOpenedMs"].toVariant().toLongLong();
        if (!r.path.isEmpty())
            m_recentProjects.push_back(std::move(r));
    }
    std::stable_sort(m_recentProjects.begin(), m_recentProjects.end(),
                     [](const RecentProject& a, const RecentProject& b) {
                         return a.lastOpenedMs > b.lastOpenedMs;
                     });
    if (static_cast<int>(m_recentProjects.size()) > MaxRecentProjects)
        m_recentProjects.resize(MaxRecentProjects);

    // Migrate the legacy single-path setting into the recents list once.
    if (m_recentProjects.empty() && !lastProjectPath.isEmpty())
        addRecentProject(lastProjectPath);
}
