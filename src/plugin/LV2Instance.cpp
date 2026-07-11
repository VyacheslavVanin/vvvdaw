#include "LV2Instance.h"
#include <cstring>
#include <QJsonObject>
#include <QJsonArray>

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

    uint32_t numPorts = lilv_plugin_get_num_ports(m_plugin);
    for (uint32_t i = 0; i < numPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);

        bool isInput = lilv_port_is_a(m_plugin, port, inputPort);
        bool isOutput = lilv_port_is_a(m_plugin, port, outputPort);
        bool isAudio = lilv_port_is_a(m_plugin, port, audioPort);
        bool isControl = lilv_port_is_a(m_plugin, port, controlPort);

        if (isAudio && isInput) m_audioInPorts.push_back(i);
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
    if (!m_plugin) return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    const LV2_Feature* features[] = { nullptr };

    m_instance = lilv_plugin_instantiate(m_plugin, sampleRate, features);
    if (!m_instance) return false;

    lilv_instance_activate(m_instance);
    m_active = true;
    return true;
}

bool LV2Instance::deactivate() {
    if (!m_active || !m_instance) return true;
    lilv_instance_deactivate(m_instance);
    lilv_instance_free(m_instance);
    m_instance = nullptr;
    m_active = false;
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
