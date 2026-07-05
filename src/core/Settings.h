#pragma once
#include <QString>
#include <QJsonObject>

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

    QString lastProjectPath;

private:
    QString configFilePath() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};
