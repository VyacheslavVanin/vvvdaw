#pragma once
#include <QString>
#include <QJsonObject>
#include <vector>
#include <cstdint>

struct PluginPortInfo {
    enum class Type { Audio, Control };
    enum class Direction { Input, Output };

    Type type;
    Direction direction;
    QString name;
    int index;
    float defaultValue;
    float minValue;
    float maxValue;
};

class PluginInstance {
public:
    virtual ~PluginInstance() = default;

    virtual bool load(const QString& path) = 0;
    virtual bool activate(double sampleRate, int maxBlockSize) = 0;
    virtual bool deactivate() = 0;
    virtual bool process(float** inputBuffers, float** outputBuffers,
                         int numSamples, int numChannels) = 0;

    virtual QString name() const = 0;
    virtual QString vendor() const = 0;
    virtual QString pluginId() const = 0;
    virtual QString filePath() const = 0;

    virtual bool isActive() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;

    virtual int latencySamples() const = 0;

    virtual std::vector<PluginPortInfo> ports() const = 0;

    virtual bool hasEditor() const = 0;
    virtual void* createEditor(void* parentWindow) = 0;
    virtual void destroyEditor() = 0;
    virtual void resizeEditor(int width, int height) = 0;

    virtual QJsonObject stateToJson() const = 0;
    virtual void stateFromJson(const QJsonObject& json) = 0;
};
