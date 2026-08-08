#include "InstrumentCommands.h"
#include "model/Project.h"
#include "model/Instrument.h"
#include "plugin/PluginChain.h"
#include "plugin/PluginManager.h"
#include <QJsonObject>

// --- AddInstrumentCommand ---

AddInstrumentCommand::AddInstrumentCommand(Project& project)
    : m_project(project) {}

void AddInstrumentCommand::execute() {
    Instrument inst;
    inst.setName(QString("Instrument %1").arg(m_project.instruments().size() + 1));
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
