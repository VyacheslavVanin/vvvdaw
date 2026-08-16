#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QTimer>
#include <vector>

class Project;
struct AudioBus;
class PluginInstance;
class PluginListWidget;
class PluginManager;
class AudioEngine;
class BusLevelMeter;
class BusSendsWidget;

class BusPanelWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit BusPanelWidget(Project& project, QWidget* parent = nullptr);

    void rebuild();
    void refreshOutCombos();
    void setPluginManager(PluginManager* pm) { m_pluginManager = pm; }
    void setAudioEngine(AudioEngine* engine) { m_engine = engine; }
    void setAudioParams(double sampleRate, int bufferSize) { m_sampleRate = sampleRate; m_bufferSize = bufferSize; }

    static constexpr int kControlsWidth = 68;
    static constexpr int kPluginPanelWidth = 240;

signals:
    void busChanged();
    void addBusRequested();
    void removeBusRequested(int index);
    void openBusPluginEditorRequested(int busIndex, PluginInstance* plugin);
    void busPluginWillBeRemoved(PluginInstance* plugin);
    void busPluginAddRequested(int busIndex, const QString& type, const QString& path);
    void busPluginRemoved(int busIndex, int pluginIndex);
    void busPluginWillBeMoved(int busIndex, int from, int to);
    void busPluginWillBeToggled(int busIndex);
    void busVolumeWillChange(int busIndex, float oldVal, float newVal);
    void busPanWillChange(int busIndex, float oldVal, float newVal);
    void busSoloWillChange(int busIndex, bool oldVal, bool newVal);
    void busMuteWillChange(int busIndex, bool oldVal, bool newVal);
    void busNameWillChange(int busIndex, const QString& oldName, const QString& newName);
    void busOutputWillChange(int busIndex, int oldVal, int newVal);
    void busSendAddRequested(int busIndex);
    void busSendRemoveRequested(int busIndex, int sendIndex);
    void busSendTargetWillChange(int busIndex, int sendIndex, int oldBus, int newBus);
    void busSendLevelWillChange(int busIndex, int sendIndex, float oldLevel, float newLevel);
    void busSendPreWillChange(int busIndex, int sendIndex, bool oldPre, bool newPre);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct BusRow {
        QWidget* widget = nullptr;
        QLineEdit* nameEdit = nullptr;
        QPushButton* soloButton = nullptr;
        QPushButton* muteButton = nullptr;
        QComboBox* outCombo = nullptr;
        QSlider* panSlider = nullptr;
        QSlider* volumeSlider = nullptr;
        BusLevelMeter* levelMeter = nullptr;
        QPushButton* panelToggle = nullptr;
        // Combined plugins + sends panel (plugins on top, sends below),
        // revealed by the single panelToggle.
        QWidget* fxPanel = nullptr;
        PluginListWidget* pluginList = nullptr;
        BusSendsWidget* sendsList = nullptr;
    };

    void updateMeters();

    Project& m_project;
    PluginManager* m_pluginManager = nullptr;
    AudioEngine* m_engine = nullptr;
    double m_sampleRate = 48000;
    int m_bufferSize = 512;
    QWidget* m_container = nullptr;
    QHBoxLayout* m_containerLayout = nullptr;
    std::vector<BusRow> m_busRows;
    // Per-bus "plugins + sends panel open" state, carried across rebuild() so a
    // full panel refresh (e.g. after adding a plugin or send) does not collapse
    // an explicitly opened combined panel.
    std::vector<bool> m_panelOpen;
    QTimer* m_meterTimer = nullptr;
};
