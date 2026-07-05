#pragma once
#include <QString>

struct DeviceInfo {
    int id = -1;
    QString name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
};
