#pragma once
#include <QString>
#include <QJsonObject>
#include "plugin/PluginChain.h"

class PluginManager;

class AudioBus {
public:
    AudioBus() = default;

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    float pan() const { return m_pan; }
    void setPan(float pan) { m_pan = pan; }

    float volume() const { return m_volume; }
    void setVolume(float volume) { m_volume = volume; }

    int outputBusIndex() const { return m_outputBusIndex; }
    void setOutputBusIndex(int idx) { m_outputBusIndex = idx; }

    bool isSolo() const { return m_solo; }
    void setSolo(bool solo) { m_solo = solo; }

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted) { m_muted = muted; }

    bool removable() const { return m_removable; }
    void setRemovable(bool removable) { m_removable = removable; }

    PluginChain& pluginChain() { return m_pluginChain; }
    const PluginChain& pluginChain() const { return m_pluginChain; }

    QJsonObject toJson() const;
    static AudioBus fromJson(const QJsonObject& obj, PluginManager* manager = nullptr);

private:
    QString m_name;
    float m_pan = 0.0f;
    float m_volume = 1.0f;
    int m_outputBusIndex = 0;
    bool m_solo = false;
    bool m_muted = false;
    bool m_removable = true;
    PluginChain m_pluginChain;
};
