#pragma once
#include "PluginInstance.h"
#include <lilv/lilv.h>
#include <vector>

class LV2Instance : public PluginInstance {
public:
    LV2Instance(LilvWorld* world, const LilvPlugin* plugin);
    ~LV2Instance() override;

    bool load(const QString& path) override;
    bool activate(double sampleRate, int maxBlockSize) override;
    bool deactivate() override;
    bool process(float** inputBuffers, float** outputBuffers,
                 int numSamples, int numChannels) override;

    QString name() const override;
    QString vendor() const override;
    QString pluginId() const override;
    QString filePath() const override;

    bool isActive() const override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;

    int latencySamples() const override;
    std::vector<PluginPortInfo> ports() const override;

    bool hasEditor() const override;
    void* createEditor(void* parentWindow) override;
    void destroyEditor() override;
    void resizeEditor(int width, int height) override;

    QJsonObject stateToJson() const override;
    void stateFromJson(const QJsonObject& json) override;

    const LilvPlugin* lilvPlugin() const { return m_plugin; }

private:
    bool m_enabled = true;
    bool m_active = false;
    double m_sampleRate = 48000;
    int m_maxBlockSize = 512;
    QString m_name;
    QString m_uri;

    const LilvPlugin* m_plugin = nullptr;
    LilvWorld* m_world = nullptr;
    LilvInstance* m_instance = nullptr;

    std::vector<uint32_t> m_audioInPorts;
    std::vector<uint32_t> m_audioOutPorts;
    std::vector<uint32_t> m_controlInPorts;
    std::vector<uint32_t> m_controlOutPorts;
    std::vector<float> m_controlValues;
    std::vector<float> m_controlDefaults;
};
