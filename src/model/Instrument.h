#pragma once
#include <QString>
#include <QJsonObject>
#include <memory>
#include "plugin/PluginInstance.h"
#include "plugin/PluginChain.h"

class PluginManager;

class Instrument {
public:
    Instrument() = default;
    ~Instrument() = default;
    Instrument(Instrument&&) noexcept = default;
    Instrument& operator=(Instrument&&) noexcept = default;
    Instrument(const Instrument&) = delete;
    Instrument& operator=(const Instrument&) = delete;

    // One output channel of the synth routed to a (stereo) bus, with an
    // optional user-facing name.
    struct ChannelRoute {
        int busIndex = 0;
        QString name;
    };

    const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    PluginInstance* synth() const { return m_synth.get(); }
    void setSynth(std::unique_ptr<PluginInstance> synth) { m_synth = std::move(synth); }
    std::unique_ptr<PluginInstance> takeSynth() { return std::move(m_synth); }

    PluginChain& effects() { return m_effects; }
    const PluginChain& effects() const { return m_effects; }

    int outputBusIndex() const { return m_outputBusIndex; }
    void setOutputBusIndex(int idx) { m_outputBusIndex = idx; }

    bool isMultiChannel() const { return m_multiChannel; }
    void setMultiChannel(bool enabled) { m_multiChannel = enabled; }

    std::vector<ChannelRoute>& channelRoutes() { return m_channelRoutes; }
    const std::vector<ChannelRoute>& channelRoutes() const { return m_channelRoutes; }
    void setChannelRoutes(std::vector<ChannelRoute> routes) { m_channelRoutes = std::move(routes); }

    QJsonObject routingToJson() const;
    void applyRoutingFromJson(const QJsonObject& obj);

    float volume() const { return m_volume; }
    void setVolume(float volume) { m_volume = volume; }

    float pan() const { return m_pan; }
    void setPan(float pan) { m_pan = pan; }

    bool isMuted() const { return m_muted; }
    void setMuted(bool muted) { m_muted = muted; }

    bool isSolo() const { return m_solo; }
    void setSolo(bool solo) { m_solo = solo; }

    QJsonObject toJson() const;
    static Instrument fromJson(const QJsonObject& obj, PluginManager* manager = nullptr);

private:
    QString m_name;
    std::unique_ptr<PluginInstance> m_synth;
    PluginChain m_effects;
    int m_outputBusIndex = 0;
    bool m_multiChannel = false;
    std::vector<ChannelRoute> m_channelRoutes;
    float m_volume = 1.0f;
    float m_pan = 0.0f;
    bool m_muted = false;
    bool m_solo = false;
};
