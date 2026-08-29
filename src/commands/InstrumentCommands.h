#pragma once
#include "core/UndoCommand.h"
#include "core/Constants.h"
#include "model/Instrument.h"
#include "SetValueCommand.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class Project;
class PluginManager;

class AddInstrumentCommand : public UndoCommand {
public:
    // Adds a new instrument. When synthJson is non-empty ({type, path}), the
    // instrument is created already carrying that synth plugin (the "Add
    // Instrument" picker flow); undo removes the instrument and its synth.
    AddInstrumentCommand(Project& project,
                         PluginManager* manager = nullptr,
                         double sampleRate = vvvdaw::DefaultSampleRate,
                         int bufferSize = vvvdaw::DefaultBufferSize,
                         QJsonObject synthJson = {});
    void execute() override;
    void undo() override;
    int id() const override { return 70; }
private:
    Project& m_project;
    PluginManager* m_manager = nullptr;
    double m_sampleRate;
    int m_bufferSize;
    QJsonObject m_synthJson;
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

// Adds an instrument (carrying the chosen synth) and a MIDI track routed to
// it in one atomic undo step. Used by the Track menu "Add Instrument Track":
// one undo removes both the instrument and the track.
class AddInstrumentTrackCommand : public UndoCommand {
public:
    AddInstrumentTrackCommand(Project& project, PluginManager* manager,
                              double sampleRate, int bufferSize,
                              QJsonObject synthJson,
                              QString instrumentName = {},
                              QString trackName = {});
    void execute() override;
    void undo() override;
    int id() const override { return 81; }
private:
    Project& m_project;
    PluginManager* m_manager = nullptr;
    double m_sampleRate;
    int m_bufferSize;
    QJsonObject m_synthJson;
    QString m_instrumentName;
    QString m_trackName;
    QJsonObject m_savedInstrument;
    QJsonObject m_savedTrack;
    int m_instrumentIndex = -1;
    int m_trackIndex = -1;
    bool m_redoing = false;
    // Split from execute() to keep each function's cyclomatic complexity low.
    void create();
    void restore();
    void insertInstrument(Instrument inst);
    void insertTrack(Track track);
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

class SetInstrumentRoutingCommand : public UndoCommand {
public:
    SetInstrumentRoutingCommand(Project& project, int index,
                                QJsonObject oldRouting, QJsonObject newRouting);
    void execute() override;
    void undo() override;
    int id() const override { return 79; }
private:
    Project& m_project;
    int m_index;
    QJsonObject m_oldRouting;
    QJsonObject m_newRouting;
};

// Records buses created by the channel-routing dialog ("create bus per
// channel") together with the routing change that assigns each channel to its
// own new bus. The dialog adds the buses to the project before the command is
// pushed, so execute() only acts on redo; undo() reverts the routing and
// removes the created buses again.
class AddChannelBusesCommand : public UndoCommand {
public:
    AddChannelBusesCommand(Project& project, int instrumentIndex,
                           QJsonArray createdBuses,
                           QJsonObject routingBefore, QJsonObject routingAfter);
    void execute() override;
    void undo() override;
    int id() const override { return 80; }
private:
    Project& m_project;
    int m_instrumentIndex;
    QJsonArray m_createdBuses;
    QJsonObject m_routingBefore;
    QJsonObject m_routingAfter;
    int m_busCountBefore = -1;
    bool m_redoing = false;
};
