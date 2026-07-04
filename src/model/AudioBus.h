#pragma once
#include <QString>

struct AudioBus {
    int id = -1;
    QString name;
    int deviceId = -1;
    int channel = 0;

    bool isValid() const { return deviceId >= 0; }
};
