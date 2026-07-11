#include "Settings.h"
#include "Constants.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

Settings::Settings()
    : sampleRate(vvvdaw::DefaultSampleRate)
    , bufferSize(vvvdaw::DefaultBufferSize)
    , inputDeviceId(-1)
    , outputDeviceId(-1)
    , inputChannel(0)
    , outputChannel(0)
    , streamingThresholdSec(30)
    , mouseWheelScroll(false)
{
}

QString Settings::configFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/settings.json";
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

    QJsonArray pathsArr;
    for (const auto& path : pluginScanPaths)
        pathsArr.append(path);
    obj["pluginScanPaths"] = pathsArr;

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
    if (obj.contains("pluginScanPaths")) {
        pluginScanPaths.clear();
        QJsonArray arr = obj["pluginScanPaths"].toArray();
        for (const auto& v : arr)
            pluginScanPaths.push_back(v.toString());
    }
}
