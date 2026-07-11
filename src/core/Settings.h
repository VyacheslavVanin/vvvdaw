#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>

class Settings {
public:
    Settings();

    void load();
    void save();

    int sampleRate;
    int bufferSize;
    int inputDeviceId;
    int outputDeviceId;
    int inputChannel;
    int outputChannel;
    int streamingThresholdSec;
    bool mouseWheelScroll;

    QString lastProjectPath;
    std::vector<QString> pluginScanPaths;

private:
    QString configFilePath() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};
