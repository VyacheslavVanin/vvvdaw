#pragma once
#include "PluginInstance.h"
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/patch/patch.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <lv2/worker/worker.h>
#include <lv2/log/log.h>
#include <lv2/ui/ui.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/core/lv2.h>
#include <memory>
class LV2UIHost;
class QTimer;
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <cstdarg>
#include <utility>
#include <QByteArray>

class LV2Instance : public PluginInstance {
public:
    LV2Instance();
    ~LV2Instance() override;

    bool load(const QString& path) override;
    bool activate(double sampleRate, int maxBlockSize) override;
    bool deactivate() override;
    bool process(float** inputBuffers, float** outputBuffers,
                 int numSamples, int numChannels,
                 const MidiBuffer* midi = nullptr) override;
    bool isInstrument() const override { return m_isInstrument; }

    QString name() const override;
    QString vendor() const override;
    QString pluginId() const override;
    QString filePath() const override;

    bool isActive() const override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;

    int latencySamples() const override;
    std::vector<PluginPortInfo> ports() const override;

    void setParameter(int index, float value) override;
    float getParameter(int index) const override;

    QString getStringParameter(int index) const override;
    void setStringParameter(int index, const QString& value) override;
    QString parameterPropertyUri(int index) const override;

    QString uriForUrid(uint32_t urid) const;
    void applyStateRestore();

    bool hasEditor() const override;
    void* createEditor(void* parentWindow) override;
    void destroyEditor() override;
    void resizeEditor(int width, int height) override;
    bool getEditorSize(int& width, int& height) const override;

    QJsonObject stateToJson() const override;
    void stateFromJson(const QJsonObject& json) override;

    void setWorld(LilvWorld* world) { m_world = world; }
    void setPortWriteCallback(std::function<void(int, float)> cb) { m_portWriteCallback = std::move(cb); }

    bool hasNativeUI() const override { return !m_uiUri.isEmpty(); }

private:
    static LV2_URID uridMapCallback(LV2_URID_Map_Handle handle, const char* uri);

    struct StateStoreCtx {
        const LV2Instance* inst;
        QJsonArray* arr;
    };
    struct StateRetrieveCtx {
        LV2Instance* inst;
    };
    static LV2_State_Status stateStoreCallback(LV2_State_Handle handle,
                                               uint32_t key, const void* value,
                                               size_t size, uint32_t type,
                                               uint32_t flags);
    static const void* stateRetrieveCallback(LV2_State_Handle handle,
                                             uint32_t key, size_t* size,
                                             uint32_t* type, uint32_t* flags);

    bool m_enabled = true;
    bool m_active = false;
    bool m_isInstrument = false;
    double m_sampleRate = 48000;
    int m_maxBlockSize = 512;
    QString m_filePath;
    QString m_name;
    QString m_vendor;
    QString m_pluginId;

    LilvWorld* m_world = nullptr;
    bool m_ownsWorld = false;
    const LilvPlugin* m_plugin = nullptr;
    LilvInstance* m_instance = nullptr;

    struct PortInfo {
        const LilvPort* port = nullptr;
        PluginPortInfo::Type type;
        PluginPortInfo::Direction direction;
        QString name;
        int index;
        float defaultValue;
        float minValue;
        float maxValue;
    };
    std::vector<PortInfo> m_portInfos;

    std::vector<float*> m_audioInPorts;
    std::vector<float*> m_audioOutPorts;
    std::vector<std::vector<float>> m_audioInBuffers;
    std::vector<std::vector<float>> m_audioOutBuffers;
    std::vector<float> m_ctrlValues;
    std::vector<float*> m_ctrlPorts;
    std::vector<uint32_t> m_ctrlPortIndices;
    std::vector<uint32_t> m_audioInPortIndices;
    std::vector<uint32_t> m_audioOutPortIndices;

    struct AtomPortInfo {
        uint32_t index;
        uint32_t minSize;
        bool isInput;
        bool isMidi;
    };
    std::vector<AtomPortInfo> m_atomPorts;
    std::vector<std::vector<uint8_t>> m_atomBuffers;
    std::vector<void*> m_atomPortPtrs;

    LV2_URID_Map m_uridMap = {};
    LV2_URID m_uridSampleRate = 0;
    LV2_URID m_uridMaxBlockLength = 0;
    LV2_URID m_uridAtomSequence = 0;
    LV2_URID m_uridAtomObject = 0;
    float m_optionSampleRate = 48000;
    int m_optionMaxBlockLength = 512;
    LV2_Options_Option m_options[3] = {};

    static LV2_Worker_Status workerSchedule(LV2_Handle handle, uint32_t size, const void* data);
    static LV2_Worker_Status workerRespond(LV2_Worker_Respond_Handle handle, uint32_t size, const void* data);
    static int logSilentVPrintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, va_list ap);
    void processWorkQueue();
    void buildPortSymbolMap();
    void requestPatchGet(int atomPortIndex);
    void readOutputAtoms();
    void sendPatchSet(int portIndex, const QString& value, bool isPath);

    LV2_Worker_Schedule m_workerSchedule = { this, workerSchedule };

    LV2_Log_Log m_logger = { nullptr, nullptr, logSilentVPrintf };
    const LV2_Feature m_uridMapFeature = { LV2_URID__map, &m_uridMap };
    const LV2_Feature m_optionsFeature = { LV2_OPTIONS__options, m_options };
    const LV2_Feature m_workerScheduleFeature = { LV2_WORKER__schedule, &m_workerSchedule };
    const LV2_Feature m_logFeature = { LV2_LOG__log, &m_logger };
    const LV2_Feature* m_features[5] = { &m_uridMapFeature, &m_optionsFeature, &m_workerScheduleFeature, &m_logFeature, nullptr };

    std::vector<std::pair<std::string, LV2_URID>> m_uridCache;
    mutable std::mutex m_uridMutex;

    // Patch/atom URIDs
    LV2_URID m_uridAtomPath = 0;
    LV2_URID m_uridAtomString = 0;
    LV2_URID m_uridAtomBlank = 0;
    LV2_URID m_uridEventTransfer = 0;
    LV2_URID m_uridPatchGet = 0;
    LV2_URID m_uridPatchSet = 0;
    LV2_URID m_uridPatchProperty = 0;
    LV2_URID m_uridPatchValue = 0;
    LV2_URID m_uridPatchMessage = 0;
    LV2_URID m_uridMidiEvent = 0;

    // String/path parameter storage
    std::map<int, QString> m_stringParams;
    std::map<int, LV2_URID> m_portPropertyURIDs;

    // LV2 state extension (http://lv2plug.in/ns/ext/state): per-instance
    // plugin state (e.g. drumgizmo stores its full config as an atom:Chunk).
    struct StoredStateProperty {
        QString keyUri;
        QByteArray value;
        QString typeUri;
        uint32_t flags = 0;
    };
    std::vector<StoredStateProperty> m_pendingRestore;
    bool m_hasPendingRestore = false;
    mutable std::mutex m_restoreMutex;

    // Pending patch operations (forged in process() just before run())
    bool m_pendingPatchGet = false;
    bool m_pendingPatchSet = false;
    int m_pendingPortIndex = -1;
    QString m_pendingValue;
    bool m_pendingIsPath = true;

    // LV2 worker extension (for plugins like NAM that load models via schedule_work)
    struct WorkItem {
        uint32_t size;
        std::vector<uint8_t> data;
    };
    std::vector<WorkItem> m_workQueue;
    std::vector<WorkItem> m_responseQueue;

    // UI <-> plugin atom messaging (e.g. analyser plugins like SiSco).
    struct UiMessage {
        uint32_t portIndex = 0;
        QByteArray data;
    };
    std::mutex m_uiMutex;
    std::deque<UiMessage> m_uiToPlugin;
    std::deque<UiMessage> m_pluginToUi;
    void drainUiEvents();

    // UI
    QString m_uiUri;
    QString m_uiBundlePath;
    QString m_uiBinaryPath;
    std::unique_ptr<LV2UIHost> m_uiHost;
    QTimer* m_idleTimer = nullptr;
    QWidget* m_uiContainer = nullptr;
    std::function<void(int, float)> m_portWriteCallback;
    std::map<std::string, uint32_t> m_portSymbolMap;
};
