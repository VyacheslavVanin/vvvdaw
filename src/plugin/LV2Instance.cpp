#include "LV2Instance.h"
#include <lv2/atom/util.h>
#include <cstring>
#include <cstdarg>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

LV2_URID LV2Instance::uridMapCallback(LV2_URID_Map_Handle handle, const char* uri) {
    auto* self = static_cast<LV2Instance*>(handle);
    auto it = self->m_uriMap.find(uri);
    if (it != self->m_uriMap.end())
        return it->second;
    LV2_URID id = self->m_nextUrid++;
    self->m_uriMap[uri] = id;
    self->m_uridMap[id] = uri;
    return id;
}

const char* LV2Instance::uridUnmapCallback(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
    auto* self = static_cast<LV2Instance*>(handle);
    auto it = self->m_uridMap.find(urid);
    return it != self->m_uridMap.end() ? it->second.c_str() : nullptr;
}

int LV2Instance::logPrintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, ...) {
    Q_UNUSED(handle); Q_UNUSED(type);
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return static_cast<int>(strlen(buf));
}

int LV2Instance::logVprintf(LV2_Log_Handle handle, LV2_URID type, const char* fmt, va_list ap) {
    Q_UNUSED(handle); Q_UNUSED(type);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return static_cast<int>(strlen(buf));
}

LV2_Worker_Status LV2Instance::scheduleWorkCallback(LV2_Worker_Schedule_Handle handle,
                                                     uint32_t size, const void* data) {
    Q_UNUSED(handle); Q_UNUSED(size); Q_UNUSED(data);
    return LV2_WORKER_SUCCESS;
}

LV2Instance::LV2Instance(LilvWorld* world, const LilvPlugin* plugin)
    : m_world(world)
    , m_plugin(plugin) {
    if (!m_plugin) return;

    LilvNode* nameNode = lilv_plugin_get_name(m_plugin);
    if (nameNode) {
        m_name = QString::fromUtf8(lilv_node_as_string(nameNode));
        m_uri = QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(m_plugin)));
        lilv_node_free(nameNode);
    }

    LilvNode* inputPort = lilv_new_uri(m_world, LILV_URI_INPUT_PORT);
    LilvNode* outputPort = lilv_new_uri(m_world, LILV_URI_OUTPUT_PORT);
    LilvNode* audioPort = lilv_new_uri(m_world, LILV_URI_AUDIO_PORT);
    LilvNode* controlPort = lilv_new_uri(m_world, LILV_URI_CONTROL_PORT);
    LilvNode* atomPort = lilv_new_uri(m_world, "http://lv2plug.in/ns/ext/atom#AtomPort");

    uint32_t numPorts = lilv_plugin_get_num_ports(m_plugin);
    for (uint32_t i = 0; i < numPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);

        bool isInput = lilv_port_is_a(m_plugin, port, inputPort);
        bool isOutput = lilv_port_is_a(m_plugin, port, outputPort);
        bool isAudio = lilv_port_is_a(m_plugin, port, audioPort);
        bool isControl = lilv_port_is_a(m_plugin, port, controlPort);
        bool isAtom = lilv_port_is_a(m_plugin, port, atomPort);

        if (isAtom && isInput) m_atomInPorts.push_back(i);
        else if (isAtom && isOutput) m_atomOutPorts.push_back(i);
        else if (isAudio && isInput) m_audioInPorts.push_back(i);
        else if (isAudio && isOutput) m_audioOutPorts.push_back(i);
        else if (isControl && isInput) {
            m_controlInPorts.push_back(i);
            LilvNode* defNode = nullptr;
            lilv_port_get_range(m_plugin, port, nullptr, nullptr, &defNode);
            float def = defNode ? lilv_node_as_float(defNode) : 0.0f;
            if (defNode) lilv_node_free(defNode);
            m_controlValues.push_back(def);
            m_controlDefaults.push_back(def);
        } else if (isControl && isOutput) {
            m_controlOutPorts.push_back(i);
        }
    }

    lilv_node_free(inputPort);
    lilv_node_free(outputPort);
    lilv_node_free(audioPort);
    lilv_node_free(controlPort);
    lilv_node_free(atomPort);

    m_uridFeature.handle = this;
    m_uridFeature.map = uridMapCallback;

    m_logFeature.handle = this;
    m_logFeature.printf = logPrintf;
    m_logFeature.vprintf = logVprintf;

    m_workerFeature.handle = this;
    m_workerFeature.schedule_work = scheduleWorkCallback;

    m_uridFeatureWrapper.URI = LV2_URID__map;
    m_uridFeatureWrapper.data = &m_uridFeature;

    m_logFeatureWrapper.URI = LV2_LOG__log;
    m_logFeatureWrapper.data = &m_logFeature;

    m_optionsFeatureWrapper.URI = LV2_OPTIONS__options;
    m_optionsFeatureWrapper.data = nullptr;

    m_workerFeatureWrapper.URI = LV2_WORKER__schedule;
    m_workerFeatureWrapper.data = &m_workerFeature;
}

LV2Instance::~LV2Instance() {
    deactivate();
    if (m_instance) {
        lilv_instance_free(m_instance);
        m_instance = nullptr;
    }
}

bool LV2Instance::load(const QString& /*path*/) {
    return m_plugin != nullptr;
}

bool LV2Instance::activate(double sampleRate, int maxBlockSize) {
    if (!m_plugin) { qWarning() << "LV2: no plugin"; return false; }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    LV2_URID uridAtomFloat = m_uridFeature.map(this, "http://lv2plug.in/ns/ext/atom#Float");
    LV2_URID uridAtomInt = m_uridFeature.map(this, "http://lv2plug.in/ns/ext/atom#Int");
    LV2_URID uridSampleRate = m_uridFeature.map(this, "http://lv2plug.in/ns/ext/parameters#sampleRate");
    LV2_URID uridNominalBlockLen = m_uridFeature.map(this, "http://lv2plug.in/ns/ext/buf-size#nominalBlockLength");
    LV2_URID uridMaxBlockLen = m_uridFeature.map(this, "http://lv2plug.in/ns/ext/buf-size#maxBlockLength");

    float sr = static_cast<float>(sampleRate);
    int32_t nominalBlockLen = maxBlockSize;
    int32_t maxBlockLen = maxBlockSize;
    LV2_Options_Option options[4] = {};
    options[0].context = LV2_OPTIONS_INSTANCE;
    options[0].subject = 0;
    options[0].key = uridSampleRate;
    options[0].size = sizeof(float);
    options[0].type = uridAtomFloat;
    options[0].value = &sr;

    options[1].context = LV2_OPTIONS_INSTANCE;
    options[1].subject = 0;
    options[1].key = uridNominalBlockLen;
    options[1].size = sizeof(int32_t);
    options[1].type = uridAtomInt;
    options[1].value = &nominalBlockLen;

    options[2].context = LV2_OPTIONS_INSTANCE;
    options[2].subject = 0;
    options[2].key = uridMaxBlockLen;
    options[2].size = sizeof(int32_t);
    options[2].type = uridAtomInt;
    options[2].value = &maxBlockLen;

    m_optionsFeatureWrapper.data = options;

    const LV2_Feature* features[] = {
        &m_uridFeatureWrapper,
        &m_logFeatureWrapper,
        &m_optionsFeatureWrapper,
        &m_workerFeatureWrapper,
        nullptr
    };

    m_instance = lilv_plugin_instantiate(m_plugin, sampleRate, features);
    if (!m_instance) { qWarning() << "LV2: instantiate failed for" << m_name; return false; }

    lilv_instance_activate(m_instance);
    m_active = true;
    qWarning() << "LV2: activated" << m_name
               << "audioIn:" << m_audioInPorts.size()
               << "audioOut:" << m_audioOutPorts.size()
               << "controlIn:" << m_controlInPorts.size()
               << "atomIn:" << m_atomInPorts.size()
               << "atomOut:" << m_atomOutPorts.size();
    return true;
}

bool LV2Instance::deactivate() {
    if (!m_active || !m_instance) return true;
    lilv_instance_deactivate(m_instance);
    lilv_instance_free(m_instance);
    m_instance = nullptr;
    m_active = false;
    m_atomInBuf.clear();
    m_atomOutBuf.clear();
    return true;
}

bool LV2Instance::process(float** inputBuffers, float** outputBuffers,
                          int numSamples, int numChannels) {
    if (!m_active || !m_instance || !m_enabled) {
        if (outputBuffers && inputBuffers) {
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy(outputBuffers[ch], inputBuffers[ch], numSamples * sizeof(float));
        }
        return true;
    }

    for (size_t i = 0; i < m_audioInPorts.size() && static_cast<int>(i) < numChannels; ++i)
        lilv_instance_connect_port(m_instance, m_audioInPorts[i], inputBuffers[i]);

    for (size_t i = 0; i < m_audioOutPorts.size() && static_cast<int>(i) < numChannels; ++i)
        lilv_instance_connect_port(m_instance, m_audioOutPorts[i], outputBuffers[i]);

    for (size_t i = 0; i < m_controlInPorts.size() && i < m_controlValues.size(); ++i)
        lilv_instance_connect_port(m_instance, m_controlInPorts[i], &m_controlValues[i]);

    for (size_t i = 0; i < m_controlOutPorts.size(); ++i) {
        static float dummy;
        lilv_instance_connect_port(m_instance, m_controlOutPorts[i], &dummy);
    }

    LV2_URID uridAtomSequence = m_uridFeature.map(this, LV2_ATOM__Sequence);
    LV2_URID uridFrameTime = m_uridFeature.map(this, LV2_ATOM__frameTime);

    for (size_t i = 0; i < m_atomInPorts.size(); ++i) {
        if (m_atomInBuf.empty()) {
            m_atomInBuf.resize(2048, 0);
            auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomInBuf.data());
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
            seq->atom.type = uridAtomSequence;
            seq->body.unit = uridFrameTime;
            seq->body.pad = 0;
        }
        lilv_instance_connect_port(m_instance, m_atomInPorts[i], m_atomInBuf.data());
    }

    for (size_t i = 0; i < m_atomOutPorts.size(); ++i) {
        if (m_atomOutBuf.empty()) {
            m_atomOutBuf.resize(2048, 0);
            auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomOutBuf.data());
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
            seq->atom.type = uridAtomSequence;
            seq->body.unit = uridFrameTime;
            seq->body.pad = 0;
        }
        lilv_instance_connect_port(m_instance, m_atomOutPorts[i], m_atomOutBuf.data());
    }

    lilv_instance_run(m_instance, numSamples);
    return true;
}

QString LV2Instance::name() const { return m_name; }
QString LV2Instance::vendor() const { return QString(); }
QString LV2Instance::pluginId() const { return m_uri; }
QString LV2Instance::filePath() const { return QString(); }
bool LV2Instance::isActive() const { return m_active; }
void LV2Instance::setEnabled(bool enabled) { m_enabled = enabled; }
bool LV2Instance::isEnabled() const { return m_enabled; }

int LV2Instance::latencySamples() const {
    return 0;
}

std::vector<PluginPortInfo> LV2Instance::ports() const {
    std::vector<PluginPortInfo> result;
    if (!m_plugin) return result;

    LilvNode* inputPort = lilv_new_uri(m_world, LILV_URI_INPUT_PORT);
    LilvNode* outputPort = lilv_new_uri(m_world, LILV_URI_OUTPUT_PORT);
    LilvNode* audioPort = lilv_new_uri(m_world, LILV_URI_AUDIO_PORT);
    LilvNode* controlPort = lilv_new_uri(m_world, LILV_URI_CONTROL_PORT);

    uint32_t numPorts = lilv_plugin_get_num_ports(m_plugin);
    for (uint32_t i = 0; i < numPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);

        bool isInput = lilv_port_is_a(m_plugin, port, inputPort);
        bool isOutput = lilv_port_is_a(m_plugin, port, outputPort);
        bool isAudio = lilv_port_is_a(m_plugin, port, audioPort);
        bool isControl = lilv_port_is_a(m_plugin, port, controlPort);

        PluginPortInfo pi;
        pi.index = static_cast<int>(i);

        LilvNode* nameNode = lilv_port_get_name(m_plugin, port);
        pi.name = nameNode ? QString::fromUtf8(lilv_node_as_string(nameNode)) : QString("Port %1").arg(i);
        if (nameNode) lilv_node_free(nameNode);

        if (isAudio) pi.type = PluginPortInfo::Type::Audio;
        else if (isControl) pi.type = PluginPortInfo::Type::Control;
        else continue;

        pi.direction = isInput ? PluginPortInfo::Direction::Input : PluginPortInfo::Direction::Output;

        if (isControl && isInput) {
            LilvNode* minNode = nullptr;
            LilvNode* maxNode = nullptr;
            LilvNode* defNode = nullptr;
            lilv_port_get_range(m_plugin, port, &minNode, &maxNode, &defNode);
            if (minNode) { pi.minValue = lilv_node_as_float(minNode); lilv_node_free(minNode); }
            if (maxNode) { pi.maxValue = lilv_node_as_float(maxNode); lilv_node_free(maxNode); }
            if (defNode) { pi.defaultValue = lilv_node_as_float(defNode); lilv_node_free(defNode); }
        }

        result.push_back(pi);
    }

    lilv_node_free(inputPort);
    lilv_node_free(outputPort);
    lilv_node_free(audioPort);
    lilv_node_free(controlPort);
    return result;
}

bool LV2Instance::hasEditor() const {
    if (!m_plugin || !m_world) return false;
    const LilvUIs* uis = lilv_plugin_get_uis(m_plugin);
    return uis && lilv_uis_size(uis) > 0;
}

void* LV2Instance::createEditor(void* parentWindow) {
    Q_UNUSED(parentWindow);
    return nullptr;
}

void LV2Instance::destroyEditor() {}

void LV2Instance::resizeEditor(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
}

QJsonObject LV2Instance::stateToJson() const {
    QJsonObject json;
    json["type"] = "lv2";
    json["uri"] = m_uri;
    json["enabled"] = m_enabled;

    QJsonArray arr;
    for (float v : m_controlValues)
        arr.append(static_cast<double>(v));
    json["controlValues"] = arr;

    return json;
}

void LV2Instance::stateFromJson(const QJsonObject& json) {
    if (json.contains("enabled"))
        m_enabled = json["enabled"].toBool(true);

    if (json.contains("controlValues")) {
        QJsonArray arr = json["controlValues"].toArray();
        for (int i = 0; i < arr.size() && i < static_cast<int>(m_controlValues.size()); ++i)
            m_controlValues[i] = static_cast<float>(arr[i].toDouble());
    }
}
