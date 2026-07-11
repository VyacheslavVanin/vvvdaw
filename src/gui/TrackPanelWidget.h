#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <vector>

class Track;
struct AudioBus;
struct DeviceInfo;
class PluginListWidget;
class PluginInstance;
class PluginManager;

class TrackPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackPanelWidget(Track* track, QWidget* parent = nullptr);

    void setTrack(Track* track) { m_track = track; }
    Track* track() const { return m_track; }
    void updateFromTrack();
    void setAlternateRow(bool alternate);
    void setPluginManager(PluginManager* pm);

    void updateBusList(const std::vector<AudioBus>& buses);
    void updateInputDeviceList(const std::vector<DeviceInfo>& devices);

    PluginListWidget* pluginList() const { return m_pluginList; }

signals:
    void armToggled(bool armed);
    void soloToggled(bool solo);
    void muteToggled(bool muted);
    void monitorToggled(bool monitoring);
    void panChanged(float pan);
    void volumeChanged(float volume);
    void outputBusChanged(int index);
    void inputDeviceChanged(int deviceId);
    void deleteRequested();
    void addTrackRequested();
    void beforeModify();
    void openPluginEditorRequested(PluginInstance* plugin);
    void pluginWillBeRemoved(PluginInstance* plugin);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    Track* m_track = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QPushButton* m_armButton = nullptr;
    QPushButton* m_soloButton = nullptr;
    QPushButton* m_muteButton = nullptr;
    QPushButton* m_monitorButton = nullptr;
    QSlider* m_panSlider = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QComboBox* m_outputBusCombo = nullptr;
    QComboBox* m_inputDeviceCombo = nullptr;
    PluginListWidget* m_pluginList = nullptr;
};
