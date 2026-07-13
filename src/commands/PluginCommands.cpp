#include "PluginCommands.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "plugin/VST3Instance.h"
#include "plugin/PluginManager.h"

// --- AddPluginCommand ---

AddPluginCommand::AddPluginCommand(PluginChain& chain, QJsonObject pluginJson,
                                   PluginManager* manager, double sampleRate, int bufferSize)
    : m_chain(chain), m_pluginJson(pluginJson), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {}

void AddPluginCommand::execute() {
    QString type = m_pluginJson["type"].toString();
    if (type == "vst3") {
        auto instance = std::make_unique<VST3Instance>();
        if (instance->load(m_pluginJson["path"].toString())) {
            instance->activate(m_sampleRate, m_bufferSize);
            m_chain.addPlugin(std::move(instance));
        }
    }
}

void AddPluginCommand::undo() {
    m_chain.removePlugin(m_chain.count() - 1);
}

// --- RemovePluginCommand ---

RemovePluginCommand::RemovePluginCommand(PluginChain& chain, int index,
                                         PluginManager* manager, double sampleRate, int bufferSize)
    : m_chain(chain), m_index(index), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {
    if (index >= 0 && index < chain.count())
        m_savedPlugin = chain.plugin(index)->stateToJson();
}

void RemovePluginCommand::execute() {
    m_chain.removePlugin(m_index);
}

void RemovePluginCommand::undo() {
    QString type = m_savedPlugin["type"].toString();
    if (type == "vst3") {
        auto instance = std::make_unique<VST3Instance>();
        if (instance->load(m_savedPlugin["path"].toString())) {
            instance->activate(m_sampleRate, m_bufferSize);
            instance->stateFromJson(m_savedPlugin);
            m_chain.addPlugin(std::move(instance));
        }
    }
}

// --- MovePluginCommand ---

MovePluginCommand::MovePluginCommand(PluginChain& chain, int fromIndex, int toIndex)
    : m_chain(chain), m_fromIndex(fromIndex), m_toIndex(toIndex) {}

void MovePluginCommand::execute() {
    m_chain.movePlugin(m_fromIndex, m_toIndex);
}

void MovePluginCommand::undo() {
    m_chain.movePlugin(m_toIndex, m_fromIndex);
}

// --- TogglePluginCommand ---

TogglePluginCommand::TogglePluginCommand(PluginInstance* plugin, bool newValue)
    : m_plugin(plugin), m_oldValue(plugin->isEnabled()), m_newValue(newValue) {}

void TogglePluginCommand::execute() {
    m_plugin->setEnabled(m_newValue);
}

void TogglePluginCommand::undo() {
    m_plugin->setEnabled(m_oldValue);
}
