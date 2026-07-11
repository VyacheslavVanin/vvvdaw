#pragma once
#include <QString>
#include "plugin/PluginChain.h"

struct AudioBus {
    QString name;
    float pan = 0.0f;
    float volume = 1.0f;
    int outputBusIndex = 0;
    bool removable = true;
    PluginChain pluginChain;
};
