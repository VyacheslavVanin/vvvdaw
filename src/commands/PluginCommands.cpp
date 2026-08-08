#include "PluginCommands.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginManager.h"

// --- AddPluginCommand ---

AddPluginCommand::AddPluginCommand(PluginChain& chain, QJsonObject pluginJson,
                                   PluginManager* manager, double sampleRate, int bufferSize)
    : m_chain(chain), m_pluginJson(pluginJson), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {}

void AddPluginCommand::execute() {
    auto instance = PluginChain::createInstance(m_pluginJson, m_manager);
    if (instance) {
        instance->activate(m_sampleRate, m_bufferSize);
        m_addedPlugin = instance.get();
        m_chain.addPlugin(std::move(instance));
    }
}

void AddPluginCommand::undo() {
    if (m_beforeRemove && m_addedPlugin)
        m_beforeRemove(m_addedPlugin);
    m_chain.removePlugin(m_chain.count() - 1);
    m_addedPlugin = nullptr;
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
    auto instance = PluginChain::createInstance(m_savedPlugin, m_manager);
    if (instance) {
        instance->activate(m_sampleRate, m_bufferSize);
        m_chain.addPlugin(std::move(instance));
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

// --- SetPluginPathParameterCommand ---

SetPluginPathParameterCommand::SetPluginPathParameterCommand(PluginChain& chain, PluginInstance* plugin,
                                                             int paramIndex,
                                                             QString oldValue, QString newValue)
    : m_chain(chain), m_pluginIndex(chain.indexOf(plugin)), m_paramIndex(paramIndex),
      m_oldValue(std::move(oldValue)), m_newValue(std::move(newValue)) {}

PluginInstance* SetPluginPathParameterCommand::resolvePlugin() const {
    return m_chain.plugin(m_pluginIndex);
}

void SetPluginPathParameterCommand::execute() {
    if (auto* p = resolvePlugin())
        p->setStringParameter(m_paramIndex, m_newValue);
}

void SetPluginPathParameterCommand::undo() {
    if (auto* p = resolvePlugin())
        p->setStringParameter(m_paramIndex, m_oldValue);
}

bool SetPluginPathParameterCommand::mergeWith(const UndoCommand* other) {
    (void)other;
    return false;
}

// --- SetPluginParameterCommand ---

SetPluginParameterCommand::SetPluginParameterCommand(PluginChain& chain, PluginInstance* plugin,
                                                     int paramIndex,
                                                     float oldValue, float newValue)
    : m_chain(chain), m_pluginIndex(chain.indexOf(plugin)), m_paramIndex(paramIndex),
      m_oldValue(oldValue), m_newValue(newValue) {}

PluginInstance* SetPluginParameterCommand::resolvePlugin() const {
    return m_chain.plugin(m_pluginIndex);
}

void SetPluginParameterCommand::execute() {
    if (auto* p = resolvePlugin())
        p->setParameter(m_paramIndex, m_newValue);
}

void SetPluginParameterCommand::undo() {
    if (auto* p = resolvePlugin())
        p->setParameter(m_paramIndex, m_oldValue);
}

bool SetPluginParameterCommand::mergeWith(const UndoCommand* other) {
    auto* cmd = dynamic_cast<const SetPluginParameterCommand*>(other);
    if (!cmd) return false;
    if (&m_chain != &cmd->m_chain || m_pluginIndex != cmd->m_pluginIndex || m_paramIndex != cmd->m_paramIndex) return false;
    m_newValue = cmd->m_newValue;
    return true;
}
