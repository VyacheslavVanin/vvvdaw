#pragma once
#include <QDialog>
#include <vector>
#include "model/Instrument.h"

class QTableWidget;
class Project;
class PluginInstance;

// Lets the user map each audio output channel of an instrument's synth to a
// bus and rename the channels for readability. Editing works on a copy of the
// instrument's routes; applyTo() commits the result. A button can create one
// bus per channel (added to the project immediately) and assign each channel
// to its own bus; the created buses are removed again if the dialog is
// rejected.
class ChannelRoutingDialog : public QDialog {
    Q_OBJECT
public:
    ChannelRoutingDialog(Project& project, Instrument& instrument,
                         PluginInstance* synth,
                         QWidget* parent = nullptr);

    void accept() override;
    void reject() override;

    void applyTo(Instrument& instrument) const;

    int createdBusCount() const { return static_cast<int>(m_createdBusIndices.size()); }
    const std::vector<int>& createdBusIndices() const { return m_createdBusIndices; }

private:
    void createBusesForChannels();

    Project& m_project;
    Instrument& m_instrument;
    std::vector<Instrument::ChannelRoute> m_routes;
    std::vector<int> m_createdBusIndices;
    QTableWidget* m_table = nullptr;
};
