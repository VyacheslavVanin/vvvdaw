#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QHBoxLayout>
#include <vector>
#include <utility>

class Track;
struct AudioBus;
struct DeviceInfo;

class TrackPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackPanelWidget(Track* track, QWidget* parent = nullptr);

    void setTrack(Track* track) { m_track = track; }
    Track* track() const { return m_track; }
    void updateFromTrack();
    void setAlternateRow(bool alternate);

    void updateBusList(const std::vector<AudioBus>& buses);
    void updateInputDeviceList(const std::vector<DeviceInfo>& devices);
    void updateMidiOutputs(const std::vector<std::pair<int, QString>>& devices,
                           const std::vector<QString>& instrumentNames);

signals:
    void armToggled(bool oldValue, bool newValue);
    void soloToggled(bool oldValue, bool newValue);
    void muteToggled(bool oldValue, bool newValue);
    void monitorToggled(bool oldValue, bool newValue);
    void panChanged(float oldValue, float newValue);
    void volumeChanged(float oldValue, float newValue);
    void outputBusChanged(int oldIndex, int newIndex);
    void inputDeviceChanged(int deviceId);
    void midiOutputChanged(int deviceId, const QString& deviceName, int instrumentIndex);
    void deleteRequested();
    void addTrackRequested(int channels);
    void addMidiTrackRequested();
    void addMidiEventRequested();
    void beforeModify();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void applyTrackType();

    Track* m_track = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_channelsBadge = nullptr;
    QPushButton* m_armButton = nullptr;
    QPushButton* m_soloButton = nullptr;
    QPushButton* m_muteButton = nullptr;
    QPushButton* m_monitorButton = nullptr;
    QSlider* m_panSlider = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QComboBox* m_outputBusCombo = nullptr;
    QComboBox* m_inputDeviceCombo = nullptr;
    QWidget* m_panRow = nullptr;
    QWidget* m_volRow = nullptr;
    QWidget* m_inRow = nullptr;
};
