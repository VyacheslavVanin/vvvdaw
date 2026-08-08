#pragma once
#include "core/UndoCommand.h"
#include "core/Constants.h"
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
                            double sampleRate = vvvdaw::DefaultSampleRate, int bufferSize = vvvdaw::DefaultBufferSize);
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

class SetInstrumentVolumeCommand : public UndoCommand {
public:
    SetInstrumentVolumeCommand(Project& project, int index, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 72; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_index;
    float m_oldValue;
    float m_newValue;
};

class SetInstrumentPanCommand : public UndoCommand {
public:
    SetInstrumentPanCommand(Project& project, int index, float oldValue, float newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 73; }
    bool mergeWith(const UndoCommand* other) override;
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_index;
    float m_oldValue;
    float m_newValue;
};

class SetInstrumentMuteCommand : public UndoCommand {
public:
    SetInstrumentMuteCommand(Project& project, int index, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 74; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_index;
    bool m_oldValue;
    bool m_newValue;
};

class SetInstrumentSoloCommand : public UndoCommand {
public:
    SetInstrumentSoloCommand(Project& project, int index, bool oldValue, bool newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 75; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_index;
    bool m_oldValue;
    bool m_newValue;
};

class SetInstrumentOutputCommand : public UndoCommand {
public:
    SetInstrumentOutputCommand(Project& project, int index, int oldValue, int newValue);
    void execute() override;
    void undo() override;
    int id() const override { return 76; }
    bool requiresPluginWindowsClose() const override { return false; }
private:
    Project& m_project;
    int m_index;
    int m_oldValue;
    int m_newValue;
};

class SetInstrumentNameCommand : public UndoCommand {
public:
    SetInstrumentNameCommand(Project& project, int index, const QString& oldName, const QString& newName);
    void execute() override;
    void undo() override;
    int id() const override { return 77; }
private:
    Project& m_project;
    int m_index;
    QString m_oldName;
    QString m_newName;
};

class SetInstrumentSynthCommand : public UndoCommand {
public:
    SetInstrumentSynthCommand(Project& project, int index, QJsonObject oldSynthJson, QJsonObject newSynthJson,
                              PluginManager* manager = nullptr,
                              double sampleRate = vvvdaw::DefaultSampleRate, int bufferSize = vvvdaw::DefaultBufferSize);
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
