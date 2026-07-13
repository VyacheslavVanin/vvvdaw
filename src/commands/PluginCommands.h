#pragma once
#include "core/UndoCommand.h"
#include <QJsonObject>

class PluginChain;
class PluginManager;
class PluginInstance;

class AddPluginCommand : public UndoCommand {
public:
    AddPluginCommand(PluginChain& chain, QJsonObject pluginJson,
                     PluginManager* manager, double sampleRate, int bufferSize);
    void execute() override;
    void undo() override;
    int id() const override { return 50; }
private:
    PluginChain& m_chain;
    QJsonObject m_pluginJson;
    PluginManager* m_manager;
    double m_sampleRate;
    int m_bufferSize;
};

class RemovePluginCommand : public UndoCommand {
public:
    RemovePluginCommand(PluginChain& chain, int index,
                        PluginManager* manager, double sampleRate, int bufferSize);
    void execute() override;
    void undo() override;
    int id() const override { return 51; }
private:
    PluginChain& m_chain;
    int m_index;
    QJsonObject m_savedPlugin;
    PluginManager* m_manager;
    double m_sampleRate;
    int m_bufferSize;
};

class MovePluginCommand : public UndoCommand {
public:
    MovePluginCommand(PluginChain& chain, int fromIndex, int toIndex);
    void execute() override;
    void undo() override;
    int id() const override { return 52; }
private:
    PluginChain& m_chain;
    int m_fromIndex;
    int m_toIndex;
};

class TogglePluginCommand : public UndoCommand {
public:
    TogglePluginCommand(PluginInstance* plugin, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 53; }
private:
    PluginInstance* m_plugin;
    bool m_oldValue;
    bool m_newValue;
};
