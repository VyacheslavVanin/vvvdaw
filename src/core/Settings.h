#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>

struct DeviceInfo {
    int id = -1;
    QString name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
};

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

    QString lastProjectPath;

    std::vector<DeviceInfo> inputDevices;
    std::vector<DeviceInfo> outputDevices;

private:
    QString configFilePath() const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};
