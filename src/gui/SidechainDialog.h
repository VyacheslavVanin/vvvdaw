#pragma once
#include <QDialog>
#include <QString>

class Project;
class PluginInstance;
class QComboBox;
class QSlider;
class QPushButton;

// Chooses the source bus feeding a plugin's sidechain input, the send level and
// whether the tap is pre- or post-fader. Selecting "None" clears the
// assignment. Buses that would create a routing cycle are disabled.
class SidechainDialog : public QDialog {
    Q_OBJECT
public:
    SidechainDialog(Project& project, int ownerBusIndex, PluginInstance* plugin,
                    QWidget* parent = nullptr);

    // -1 when "None" is selected (clear the sidechain assignment).
    int sourceBus() const;
    float level() const;
    bool preFader() const;

private:
    Project& m_project;
    int m_ownerBusIndex;
    PluginInstance* m_plugin = nullptr;
    QComboBox* m_busCombo = nullptr;
    QSlider* m_levelSlider = nullptr;
    QPushButton* m_preToggle = nullptr;
};
