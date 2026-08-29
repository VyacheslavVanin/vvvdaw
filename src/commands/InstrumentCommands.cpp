#include "InstrumentCommands.h"
#include "model/Project.h"
#include "model/Instrument.h"
#include "model/AudioBus.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginManager.h"
#include <QJsonObject>
#include <QJsonArray>

// --- AddInstrumentCommand ---

AddInstrumentCommand::AddInstrumentCommand(Project& project, PluginManager* manager,
                                           double sampleRate, int bufferSize,
                                           QJsonObject synthJson)
    : m_project(project), m_manager(manager), m_sampleRate(sampleRate),
      m_bufferSize(bufferSize), m_synthJson(std::move(synthJson)) {}

void AddInstrumentCommand::execute() {
    Instrument inst;
    inst.setName(QString("Instrument %1").arg(m_project.instruments().size() + 1));
    if (!m_synthJson.isEmpty()) {
        auto synth = PluginChain::createInstance(m_synthJson, m_manager);
        if (synth) {
            synth->activate(m_sampleRate, m_bufferSize);
            inst.setSynth(std::move(synth));
        }
    }
    m_addedIndex = m_project.addInstrument(std::move(inst));
}

void AddInstrumentCommand::undo() {
    m_project.removeInstrument(m_addedIndex);
}

// --- RemoveInstrumentCommand ---

RemoveInstrumentCommand::RemoveInstrumentCommand(Project& project, int index,
                                                 PluginManager* manager,
                                                 double sampleRate, int bufferSize)
    : m_project(project), m_index(index), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {
    if (const Instrument* inst = m_project.instrumentAt(index))
        m_savedInstrument = inst->toJson();
}

void RemoveInstrumentCommand::execute() {
    m_project.removeInstrument(m_index);
}

void RemoveInstrumentCommand::undo() {
    Instrument inst = Instrument::fromJson(m_savedInstrument, m_manager);
    if (inst.synth())
        inst.synth()->activate(m_sampleRate, m_bufferSize);
    inst.effects().activate(m_sampleRate, m_bufferSize);
    if (m_index >= 0 && m_index <= static_cast<int>(m_project.instruments().size()))
        m_project.instruments().insert(m_project.instruments().begin() + m_index, std::move(inst));
    else
        m_project.addInstrument(std::move(inst));
}

// --- SetInstrumentSynthCommand ---

SetInstrumentSynthCommand::SetInstrumentSynthCommand(Project& project, int index,
                                                     QJsonObject oldSynthJson, QJsonObject newSynthJson,
                                                     PluginManager* manager,
                                                     double sampleRate, int bufferSize)
    : m_project(project), m_index(index),
      m_oldSynthJson(oldSynthJson), m_newSynthJson(newSynthJson), m_manager(manager),
      m_sampleRate(sampleRate), m_bufferSize(bufferSize) {}

void SetInstrumentSynthCommand::execute() {
    Instrument* inst = m_project.instrumentAt(m_index);
    if (!inst)
        return;
    auto synth = PluginChain::createInstance(m_newSynthJson, m_manager);
    if (synth) synth->activate(m_sampleRate, m_bufferSize);
    inst->setSynth(std::move(synth));
}

void SetInstrumentSynthCommand::undo() {
    Instrument* inst = m_project.instrumentAt(m_index);
    if (!inst)
        return;
    auto synth = PluginChain::createInstance(m_oldSynthJson, m_manager);
    if (synth) synth->activate(m_sampleRate, m_bufferSize);
    inst->setSynth(std::move(synth));
}

SetInstrumentRoutingCommand::SetInstrumentRoutingCommand(Project& project, int index,
                                                         QJsonObject oldRouting,
                                                         QJsonObject newRouting)
    : m_project(project), m_index(index),
      m_oldRouting(std::move(oldRouting)), m_newRouting(std::move(newRouting)) {}

void SetInstrumentRoutingCommand::execute() {
    Instrument* inst = m_project.instrumentAt(m_index);
    if (inst)
        inst->applyRoutingFromJson(m_newRouting);
}

void SetInstrumentRoutingCommand::undo() {
    Instrument* inst = m_project.instrumentAt(m_index);
    if (inst)
        inst->applyRoutingFromJson(m_oldRouting);
}

AddChannelBusesCommand::AddChannelBusesCommand(Project& project, int instrumentIndex,
                                               QJsonArray createdBuses,
                                               QJsonObject routingBefore,
                                               QJsonObject routingAfter)
    : m_project(project), m_instrumentIndex(instrumentIndex),
      m_createdBuses(std::move(createdBuses)),
      m_routingBefore(std::move(routingBefore)), m_routingAfter(std::move(routingAfter)),
      m_busCountBefore(project.buses().size() - static_cast<int>(m_createdBuses.size())) {}

void AddChannelBusesCommand::execute() {
    // First push only records state (the dialog already added the buses);
    // execute() re-runs when the command is redone, where the buses are gone.
    if (!m_redoing)
        return;
    for (auto v : m_createdBuses)
        m_project.addBus(AudioBus::fromJson(v.toObject()));
    if (auto* inst = m_project.instrumentAt(m_instrumentIndex))
        inst->applyRoutingFromJson(m_routingAfter);
}

void AddChannelBusesCommand::undo() {
    m_redoing = true;
    if (auto* inst = m_project.instrumentAt(m_instrumentIndex))
        inst->applyRoutingFromJson(m_routingBefore);
    // Created buses are always the highest-index ones; removing them in
    // descending order keeps the lower indices stable.
    for (int i = static_cast<int>(m_createdBuses.size()) - 1; i >= 0; --i)
        m_project.removeBus(m_busCountBefore + i);
}
