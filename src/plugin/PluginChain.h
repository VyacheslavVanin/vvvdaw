#pragma once
#include "PluginInstance.h"
#include <vector>
#include <memory>
#include <QJsonObject>

class PluginManager;

class PluginChain {
public:
    PluginChain() = default;

    void addPlugin(std::unique_ptr<PluginInstance> plugin);
    void removePlugin(int index);
    void movePlugin(int fromIndex, int toIndex);
    void clear();

    int count() const;
    PluginInstance* plugin(int index) const;
    PluginInstance* pluginById(const QString& id) const;

    bool process(float** inputBuffers, float** outputBuffers,
                 int numSamples, int numChannels) const;

    bool activate(double sampleRate, int maxBlockSize);
    bool deactivate();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json, PluginManager* manager = nullptr);

private:
    std::vector<std::unique_ptr<PluginInstance>> m_plugins;
};
