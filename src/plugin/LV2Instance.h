#pragma once
#include "PluginInstance.h"
#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
#include <lv2/log/log.h>
#include <lv2/options/options.h>
#include <lv2/atom/atom.h>
#include <lv2/worker/worker.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

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
    bool getEditorSize(int& width, int& height) const override;

    QJsonObject stateToJson() const override;
    void stateFromJson(const QJsonObject& json) override;

    const LilvPlugin* lilvPlugin() const { return m_plugin; }

private:
    static LV2_URID uridMapCallback(LV2_URID_Map_Handle handle, const char* uri);
    static const char* uridUnmapCallback(LV2_URID_Unmap_Handle handle, LV2_URID urid);
    static int logPrintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, ...);
    static int logVprintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, va_list ap);
    static LV2_Worker_Status scheduleWorkCallback(LV2_Worker_Schedule_Handle handle,
                                                   uint32_t size, const void* data);

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
    std::vector<uint32_t> m_atomInPorts;
    std::vector<uint32_t> m_atomOutPorts;
    std::vector<float> m_controlValues;
    std::vector<float> m_controlDefaults;

    std::unordered_map<std::string, LV2_URID> m_uriMap;
    std::unordered_map<LV2_URID, std::string> m_uridMap;
    LV2_URID m_nextUrid = 1;

    LV2_URID_Map m_uridFeature{};
    LV2_Log_Log m_logFeature{};
    LV2_Worker_Schedule m_workerFeature{};
    LV2_Feature m_uridFeatureWrapper{};
    LV2_Feature m_logFeatureWrapper{};
    LV2_Feature m_optionsFeatureWrapper{};
    LV2_Feature m_workerFeatureWrapper{};

    std::vector<uint8_t> m_atomInBuf;
    std::vector<uint8_t> m_atomOutBuf;
};
