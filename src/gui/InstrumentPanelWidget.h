#pragma once
#include <QScrollArea>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>

class Project;
class Instrument;
class PluginInstance;
class PluginListWidget;
class PluginManager;

class InstrumentPanelWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit InstrumentPanelWidget(Project& project, QWidget* parent = nullptr);

    void rebuild();
    void refreshOutCombos();
    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }
    void setAudioParams(double sampleRate, int bufferSize) { m_sampleRate = sampleRate; m_bufferSize = bufferSize; }

    // Shows the "Select Instrument" dialog; returns true and fills type/path
    // when an instrument plugin was chosen, false if cancelled or none exist.
    bool chooseInstrument(QString& outType, QString& outPath);

signals:
    void instrumentChanged();
    // Emitted after the user picked an instrument in the "Select Instrument"
    // dialog; the new instrument is created directly with the chosen synth.
    // Not emitted when the dialog is cancelled.
    void addInstrumentRequested(const QString& type, const QString& path);
    void removeInstrumentRequested(int index);
    void openSynthEditorRequested(int index, PluginInstance* plugin);
    void openFxEditorRequested(int index, PluginInstance* plugin);
    void pluginWillBeRemoved(PluginInstance* plugin);
    void synthAddRequested(int index, const QString& type, const QString& path);
    void synthRemoveRequested(int index);
    void fxAddRequested(int index, const QString& type, const QString& path);
    void pluginRemoved(int index, int pluginIndex);
    void pluginWillBeMoved(int index, int from, int to);
    void pluginWillBeToggled(int index);
    void volumeWillChange(int index, float oldVal, float newVal);
    void panWillChange(int index, float oldVal, float newVal);
    void soloWillChange(int index, bool oldVal, bool newVal);
    void muteWillChange(int index, bool oldVal, bool newVal);
    void nameWillChange(int index, const QString& oldName, const QString& newName);
    void outputWillChange(int index, int oldVal, int newVal);
    void routingWillChange(int index, const QJsonObject& oldRouting, const QJsonObject& newRouting);
    void channelBusesCreated(int index, const QJsonArray& createdBuses,
                             const QJsonObject& oldRouting, const QJsonObject& newRouting);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct InstrumentRow {
        QWidget* widget = nullptr;
        QLineEdit* nameEdit = nullptr;
        QPushButton* soloButton = nullptr;
        QPushButton* muteButton = nullptr;
        QComboBox* outCombo = nullptr;
        QSlider* panSlider = nullptr;
        QSlider* volumeSlider = nullptr;
        QPushButton* synthButton = nullptr;
        QPushButton* synthRemoveButton = nullptr;
        PluginListWidget* pluginList = nullptr;
    };

    void openSynthDialog(int index);
    void openAddInstrumentPicker();
    bool showInstrumentPicker(QString& outType, QString& outPath);
    void openChannelRoutingDialog(int index);
    void updateOutComboSelection(const InstrumentRow& row, int instIndex);

    Project& m_project;
    PluginManager* m_pluginManager = nullptr;
    double m_sampleRate = 48000;
    int m_bufferSize = 512;
    QWidget* m_container = nullptr;
    QHBoxLayout* m_containerLayout = nullptr;
    std::vector<InstrumentRow> m_instrumentRows;
};
