#pragma once
#include "core/UndoCommand.h"
#include "core/Constants.h"
#include "model/Instrument.h"
#include "SetValueCommand.h"
#include <QJsonObject>
#include <QString>

class Project;
class PluginManager;

class AddInstrumentCommand : public UndoCommand {
public:
    AddInstrumentCommand(Project& project);
    void execute() override;
    void undo() override;
    int id() const override { return 70; }
private:
    Project& m_project;
    int m_addedIndex = -1;
};

class RemoveInstrumentCommand : public UndoCommand {
public:
    RemoveInstrumentCommand(Project& project, int index,
                            PluginManager* manager = nullptr,
                            double sampleRate = vvvdaw::DefaultSampleRate,
                            int bufferSize = vvvdaw::DefaultBufferSize);
    void execute() override;
    void undo() override;
    int id() const override { return 71; }
private:
    Project& m_project;
    int m_index;
    QJsonObject m_savedInstrument;
    PluginManager* m_manager = nullptr;
    double m_sampleRate;
    int m_bufferSize;
};

using SetInstrumentVolumeCommand = vvvcmd::SetValueCommand<
    Instrument, float, 72, true, false, &Instrument::setVolume>;
using SetInstrumentPanCommand = vvvcmd::SetValueCommand<
    Instrument, float, 73, true, false, &Instrument::setPan>;
using SetInstrumentMuteCommand = vvvcmd::SetValueCommand<
    Instrument, bool, 74, false, false, &Instrument::setMuted>;
using SetInstrumentSoloCommand = vvvcmd::SetValueCommand<
    Instrument, bool, 75, false, false, &Instrument::setSolo>;
using SetInstrumentOutputCommand = vvvcmd::SetValueCommand<
    Instrument, int, 76, false, false, &Instrument::setOutputBusIndex>;
using SetInstrumentNameCommand = vvvcmd::SetValueCommand<
    Instrument, QString, 77, false, true, &Instrument::setName>;

class SetInstrumentSynthCommand : public UndoCommand {
public:
    SetInstrumentSynthCommand(Project& project, int index, QJsonObject oldSynthJson, QJsonObject newSynthJson,
                              PluginManager* manager = nullptr,
                              double sampleRate = vvvdaw::DefaultSampleRate,
                              int bufferSize = vvvdaw::DefaultBufferSize);
    void execute() override;
    void undo() override;
    int id() const override { return 78; }
private:
    Project& m_project;
    int m_index;
    QJsonObject m_oldSynthJson;
    QJsonObject m_newSynthJson;
    PluginManager* m_manager = nullptr;
    double m_sampleRate;
    int m_bufferSize;
};
