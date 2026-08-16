#pragma once
#include <QWidget>
#include <QPoint>
#include <vector>
#include "model/AudioBus.h"

class QVBoxLayout;
class QScrollArea;
class QPushButton;
class QComboBox;
class QSlider;
class QLayoutItem;
class Project;

// Lists the extra output sends of one bus. Each row picks the target bus, a
// send level and whether the tap happens pre- or post-fader (relative to the
// bus's volume fader). Rows are appended with the "+" button and removed from
// the row context menu. Mirrors PluginListWidget.
class BusSendsWidget : public QWidget {
    Q_OBJECT
public:
    explicit BusSendsWidget(QWidget* parent = nullptr);

    void setProject(Project* project, int busIndex) {
        m_project = project;
        m_busIndex = busIndex;
    }

    void rebuild();
    // Rebuild every row's target combo, keeping the current selection. Needed
    // when buses are added/renamed/removed or the routing graph changed.
    void refreshTargetCombos();

signals:
    void sendAddRequested(int busIndex);
    void sendRemoveRequested(int busIndex, int sendIndex);
    void sendTargetWillChange(int busIndex, int sendIndex, int oldBus, int newBus);
    void sendLevelWillChange(int busIndex, int sendIndex, float oldLevel, float newLevel);
    void sendPreWillChange(int busIndex, int sendIndex, bool oldPre, bool newPre);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct Row {
        QWidget* widget = nullptr;
        QComboBox* combo = nullptr;
        QSlider* level = nullptr;
        QPushButton* preToggle = nullptr;
    };

    AudioBus* currentBus() const;
    void buildRow(AudioBus::Send& send, int index);
    int rowAtPos(const QPoint& pos) const;

    Project* m_project = nullptr;
    int m_busIndex = -1;

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    QPushButton* m_addButton = nullptr;
    std::vector<Row> m_rows;
    QLayoutItem* m_trailingStretch = nullptr;
};
