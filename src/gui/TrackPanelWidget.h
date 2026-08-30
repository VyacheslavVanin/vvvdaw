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

class TrackPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrackPanelWidget(Track* track, QWidget* parent = nullptr);

    void setTrack(Track* track) { m_track = track; }
    Track* track() const { return m_track; }
    void updateFromTrack();
    void setAlternateRow(bool alternate);

    void updateBusList(const std::vector<AudioBus>& buses);
    void updateMidiOutputs(const std::vector<std::pair<int, QString>>& devices,
                           const std::vector<QString>& instrumentNames);

    // Height of the always-visible (name + buttons) region. The minimum a track
    // row can shrink down to.
    int nameRowHeight() const { return m_nameRowHeight; }
    // Minimum content height the panel needs to render the name row without
    // clipping (includes the panel's vertical margins).
    int minimumContentHeight() const;
    // Natural height with every type-specific control row visible.
    int fullContentHeight() const { return m_fullContentHeight; }
    // Show/hide the bottom control rows so the panel fits `contentHeight` with
    // the remaining content pinned to the top.
    void applyContentHeight(int contentHeight);
    // Number of type-specific control rows currently visible (0 when fully
    // collapsed to the name row).
    int visibleControlRowCount() const;

signals:
    void armToggled(bool oldValue, bool newValue);
    void soloToggled(bool oldValue, bool newValue);
    void muteToggled(bool oldValue, bool newValue);
    void monitorToggled(bool oldValue, bool newValue);
    void panChanged(float oldValue, float newValue);
    void volumeChanged(float oldValue, float newValue);
    void outputBusChanged(int oldIndex, int newIndex);
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
    void rebuildOptionalRows();
    // Natural height with the first `count` optional rows visible.
    int contentHeightWith(int optionalCount) const;
    void setOptionalVisible(int count);

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

    QWidget* m_nameRowWidget = nullptr;
    QWidget* m_panRowWidget = nullptr;
    QWidget* m_volRowWidget = nullptr;
    QWidget* m_outRowWidget = nullptr;
    // Optional control rows that can be collapsed when the row is short
    // (bottom-first order: out, level, pan).
    std::vector<QWidget*> m_optionalRows;
    std::vector<int> m_optionalRowHeights;

    int m_nameRowHeight = 0;
    int m_fullContentHeight = 0;
};
