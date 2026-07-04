#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

class Track;

class TrackPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackPanelWidget(Track* track, QWidget* parent = nullptr);

    void setTrack(Track* track) { m_track = track; }
    Track* track() const { return m_track; }
    void updateFromTrack();
    void setAlternateRow(bool alternate);

signals:
    void armToggled(bool armed);
    void soloToggled(bool solo);
    void muteToggled(bool muted);
    void monitorToggled(bool monitoring);
    void panChanged(float pan);
    void volumeChanged(float volume);
    void deleteRequested();
    void addTrackRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    Track* m_track = nullptr;
    QLabel* m_nameLabel = nullptr;
    QPushButton* m_armButton = nullptr;
    QPushButton* m_soloButton = nullptr;
    QPushButton* m_muteButton = nullptr;
    QPushButton* m_monitorButton = nullptr;
    QSlider* m_panSlider = nullptr;
    QSlider* m_volumeSlider = nullptr;
};
