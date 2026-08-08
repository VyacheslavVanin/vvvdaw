#pragma once
#include <QString>
#include <QJsonObject>
#include <functional>
#include <vector>
#include <cstdint>
#include "midi/MidiBuffer.h"

struct PluginPortInfo {
    enum class Type { Audio, Control, Path, String, Atom };
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
    using ParamChangeCallback = std::function<void(int paramIndex, float oldValue, float newValue)>;
    using ParamValueCallback = std::function<void(int paramIndex, float value)>;
    using StringParamValueCallback = std::function<void(int paramIndex, const QString& value)>;

    virtual ~PluginInstance() = default;

    using StringParamChangeCallback = std::function<void(int paramIndex, QString oldValue, QString newValue)>;

    virtual bool load(const QString& path) = 0;
    virtual bool activate(double sampleRate, int maxBlockSize) = 0;
    virtual bool deactivate() = 0;
    virtual bool process(float** inputBuffers, float** outputBuffers,
                         int numSamples, int numChannels,
                         const MidiBuffer* midi = nullptr) = 0;

    virtual bool isInstrument() const { return false; }

    virtual QString name() const = 0;
    virtual QString vendor() const = 0;
    virtual QString pluginId() const = 0;
    virtual QString filePath() const = 0;

    virtual bool isActive() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;

    virtual int latencySamples() const = 0;

    virtual std::vector<PluginPortInfo> ports() const = 0;

    virtual void setParameter(int index, float value) = 0;
    virtual float getParameter(int index) const = 0;

    virtual QString getStringParameter(int index) const { return {}; }
    virtual void setStringParameter(int index, const QString& value) {}
    virtual QString parameterPropertyUri(int index) const { return {}; }

    virtual bool hasNativeUI() const { return false; }
    virtual bool hasEditor() const = 0;
    virtual void* createEditor(void* parentWindow) = 0;
    virtual void destroyEditor() = 0;
    virtual void resizeEditor(int width, int height) = 0;
    virtual bool getEditorSize(int& width, int& height) const = 0;

    virtual QJsonObject stateToJson() const = 0;
    virtual void stateFromJson(const QJsonObject& json) = 0;

    void setParameterChangeCallback(ParamChangeCallback cb) { m_paramChangeCallback = std::move(cb); }
    const ParamChangeCallback& parameterChangeCallback() const { return m_paramChangeCallback; }

    void setParamValueCallback(ParamValueCallback cb) { m_paramValueCallback = std::move(cb); }
    const ParamValueCallback& paramValueCallback() const { return m_paramValueCallback; }

    void setStringParamValueCallback(StringParamValueCallback cb) { m_stringParamValueCallback = std::move(cb); }
    const StringParamValueCallback& stringParamValueCallback() const { return m_stringParamValueCallback; }

    void setStringParameterChangeCallback(StringParamChangeCallback cb) { m_stringParamChangeCallback = std::move(cb); }
    const StringParamChangeCallback& stringParameterChangeCallback() const { return m_stringParamChangeCallback; }

protected:
    // Serialize the plugin identity block shared by every backend
    // (type/path/pluginId/enabled).
    void writeIdentityToJson(QJsonObject& json, const char* type) const {
        json["type"] = type;
        json["path"] = filePath();
        json["pluginId"] = pluginId();
        json["enabled"] = isEnabled();
    }

    void readIdentityFromJson(const QJsonObject& json) {
        if (json.contains("enabled"))
            setEnabled(json["enabled"].toBool(true));
    }

    ParamChangeCallback m_paramChangeCallback;
    ParamValueCallback m_paramValueCallback;
    StringParamValueCallback m_stringParamValueCallback;
    StringParamChangeCallback m_stringParamChangeCallback;
};
