#include "PluginChain.h"
#include "VST3Instance.h"
#include "LV2Instance.h"
#include "PluginManager.h"
#include <algorithm>
#include <QJsonArray>

void PluginChain::addPlugin(std::unique_ptr<PluginInstance> plugin) {
    m_plugins.push_back(std::move(plugin));
}

void PluginChain::removePlugin(int index) {
    if (index >= 0 && index < static_cast<int>(m_plugins.size()))
        m_plugins.erase(m_plugins.begin() + index);
}

void PluginChain::movePlugin(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_plugins.size())) return;
    if (toIndex < 0 || toIndex >= static_cast<int>(m_plugins.size())) return;
    if (fromIndex == toIndex) return;

    auto item = std::move(m_plugins[fromIndex]);
    m_plugins.erase(m_plugins.begin() + fromIndex);
    m_plugins.insert(m_plugins.begin() + toIndex, std::move(item));
}

void PluginChain::clear() {
    m_plugins.clear();
}

int PluginChain::count() const {
    return static_cast<int>(m_plugins.size());
}

PluginInstance* PluginChain::plugin(int index) const {
    if (index >= 0 && index < static_cast<int>(m_plugins.size()))
        return m_plugins[index].get();
    return nullptr;
}

PluginInstance* PluginChain::pluginById(const QString& id) const {
    for (auto& p : m_plugins)
        if (p->pluginId() == id) return p.get();
    return nullptr;
}

bool PluginChain::process(float** inputBuffers, float** outputBuffers,
                          int numSamples, int numChannels) const {
    if (m_plugins.empty()) {
        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy(outputBuffers[ch], inputBuffers[ch], numSamples * sizeof(float));
        return true;
    }

    if (m_plugins.size() == 1)
        return m_plugins[0]->process(inputBuffers, outputBuffers, numSamples, numChannels);

    std::vector<std::vector<float>> tempA(numChannels, std::vector<float>(numSamples));
    std::vector<std::vector<float>> tempB(numChannels, std::vector<float>(numSamples));

    float** bufA = new float*[numChannels];
    float** bufB = new float*[numChannels];
    for (int ch = 0; ch < numChannels; ++ch) {
        bufA[ch] = tempA[ch].data();
        bufB[ch] = tempB[ch].data();
    }

    for (int ch = 0; ch < numChannels; ++ch)
        std::memcpy(bufA[ch], inputBuffers[ch], numSamples * sizeof(float));

    bool useA = true;
    for (auto& plugin : m_plugins) {
        float** inBuf = useA ? bufA : bufB;
        float** outBuf = useA ? bufB : bufA;

        plugin->process(inBuf, outBuf, numSamples, numChannels);
        useA = !useA;
    }

    float** lastBuf = useA ? bufA : bufB;
    for (int ch = 0; ch < numChannels; ++ch)
        std::memcpy(outputBuffers[ch], lastBuf[ch], numSamples * sizeof(float));

    delete[] bufA;
    delete[] bufB;
    return true;
}

bool PluginChain::activate(double sampleRate, int maxBlockSize) {
    bool ok = true;
    for (auto& p : m_plugins)
        if (!p->activate(sampleRate, maxBlockSize)) ok = false;
    return ok;
}

bool PluginChain::deactivate() {
    bool ok = true;
    for (auto& p : m_plugins)
        if (!p->deactivate()) ok = false;
    return ok;
}

QJsonObject PluginChain::toJson() const {
    QJsonObject json;
    QJsonArray arr;
    for (auto& p : m_plugins)
        arr.append(p->stateToJson());
    json["plugins"] = arr;
    return json;
}

void PluginChain::fromJson(const QJsonObject& json, PluginManager* manager) {
    m_plugins.clear();
    QJsonArray arr = json["plugins"].toArray();
    for (auto v : arr) {
        QJsonObject obj = v.toObject();
        QString type = obj["type"].toString();
        if (type == "vst3") {
            auto instance = std::make_unique<VST3Instance>();
            if (instance->load(obj["path"].toString())) {
                instance->stateFromJson(obj);
                m_plugins.push_back(std::move(instance));
            }
        } else if (type == "lv2") {
            if (manager) {
                const LilvPlugin* lilvPlugin = manager->findLV2Plugin(obj["uri"].toString());
                if (lilvPlugin) {
                    auto instance = std::make_unique<LV2Instance>(manager->lilvWorld(), lilvPlugin);
                    instance->stateFromJson(obj);
                    m_plugins.push_back(std::move(instance));
                }
            }
        }
    }
}
