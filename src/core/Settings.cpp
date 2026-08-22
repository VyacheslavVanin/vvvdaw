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
    , midiInputDeviceId(-1)
    , midiTransportControlType(1)
    , midiTransportKind(0)
    , midiTransportChannel(-1)
    , midiTransportPlayControl(110)
    , midiTransportRecordControl(111)
    , midiTransportStopControl(112)
    , streamingThresholdSec(30)
    , pluginKnobsPerRow(3)
    , busPanelVisible(false)
    , busPanelHeight(200)
    , instrumentPanelVisible(false)
    , instrumentPanelHeight(220)
    , mainWindowWidth(1400)
    , mainWindowHeight(800)
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
    obj["midiInputDeviceId"] = midiInputDeviceId;
    obj["midiTransportControlType"] = midiTransportControlType;
    obj["midiTransportKind"] = midiTransportKind;
    obj["midiTransportChannel"] = midiTransportChannel;
    obj["midiTransportPlayControl"] = midiTransportPlayControl;
    obj["midiTransportRecordControl"] = midiTransportRecordControl;
    obj["midiTransportStopControl"] = midiTransportStopControl;
    obj["lastProjectPath"] = lastProjectPath;
    obj["streamingThresholdSec"] = streamingThresholdSec;
    obj["pluginKnobsPerRow"] = pluginKnobsPerRow;
    obj["busPanelVisible"] = busPanelVisible;
    obj["busPanelHeight"] = busPanelHeight;
    obj["instrumentPanelVisible"] = instrumentPanelVisible;
    obj["instrumentPanelHeight"] = instrumentPanelHeight;
    obj["mainWindowWidth"] = mainWindowWidth;
    obj["mainWindowHeight"] = mainWindowHeight;

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
    if (obj.contains("midiInputDeviceId")) midiInputDeviceId = obj["midiInputDeviceId"].toInt(-1);
    if (obj.contains("midiTransportControlType")) midiTransportControlType = obj["midiTransportControlType"].toInt(1);
    if (obj.contains("midiTransportKind")) midiTransportKind = obj["midiTransportKind"].toInt(0);
    if (obj.contains("midiTransportChannel")) midiTransportChannel = obj["midiTransportChannel"].toInt(-1);
    if (obj.contains("midiTransportPlayControl")) midiTransportPlayControl = obj["midiTransportPlayControl"].toInt(110);
    if (obj.contains("midiTransportRecordControl")) midiTransportRecordControl = obj["midiTransportRecordControl"].toInt(111);
    if (obj.contains("midiTransportStopControl")) midiTransportStopControl = obj["midiTransportStopControl"].toInt(112);
    if (obj.contains("lastProjectPath")) lastProjectPath = obj["lastProjectPath"].toString();
    if (obj.contains("streamingThresholdSec")) streamingThresholdSec = obj["streamingThresholdSec"].toInt(30);
    if (obj.contains("pluginKnobsPerRow")) pluginKnobsPerRow = obj["pluginKnobsPerRow"].toInt(3);
    if (obj.contains("busPanelVisible")) busPanelVisible = obj["busPanelVisible"].toBool(false);
    if (obj.contains("busPanelHeight")) busPanelHeight = obj["busPanelHeight"].toInt(200);
    if (obj.contains("instrumentPanelVisible")) instrumentPanelVisible = obj["instrumentPanelVisible"].toBool(false);
    if (obj.contains("instrumentPanelHeight")) instrumentPanelHeight = obj["instrumentPanelHeight"].toInt(220);
    if (obj.contains("mainWindowWidth")) mainWindowWidth = obj["mainWindowWidth"].toInt(1400);
    if (obj.contains("mainWindowHeight")) mainWindowHeight = obj["mainWindowHeight"].toInt(800);
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
