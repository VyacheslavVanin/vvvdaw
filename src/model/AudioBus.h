#pragma once
#include <QString>
#include <QJsonObject>
#include "plugin/PluginChain.h"

class PluginManager;

struct AudioBus {
    QString name;
    float pan = 0.0f;
    float volume = 1.0f;
    int outputBusIndex = 0;
    bool solo = false;
    bool muted = false;
    bool removable = true;
    PluginChain pluginChain;

    QJsonObject toJson() const;
    static AudioBus fromJson(const QJsonObject& obj, PluginManager* manager = nullptr);
};
