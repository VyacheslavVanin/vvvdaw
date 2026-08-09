#include "ChannelRoutingDialog.h"
#include "model/Project.h"
#include "model/AudioBus.h"
#include "plugin/PluginInstance.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// Return `base` if no bus carries that name yet, otherwise append " 2", " 3",
// ... until a free name is found.
QString uniqueBusName(const Project& project, const QString& base) {
    QString candidate = base;
    int n = 2;
    bool taken = false;
    do {
        taken = false;
        for (const auto& bus : project.buses()) {
            if (bus.name() == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return candidate;
        candidate = QString("%1 %2").arg(base).arg(n++);
    } while (true);
}

} // namespace

ChannelRoutingDialog::ChannelRoutingDialog(Project& project,
                                           Instrument& instrument,
                                           PluginInstance* synth,
                                           QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_instrument(instrument) {
    setWindowTitle("Multi-channel routing");
    setMinimumSize(460, 360);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        "Map each synth output channel to a bus and give it a recognizable "
        "name. Buses receive the channel centered (equal left/right).",
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    int outCh = synth ? synth->audioOutputChannels() : 0;
    if (outCh < 1) outCh = 1;
    std::vector<QString> names = synth ? synth->audioOutputNames()
                                       : std::vector<QString>{};
    const auto& stored = m_instrument.channelRoutes();

    m_table = new QTableWidget(outCh, 2, this);
    m_table->setObjectName("channelRoutingTable");
    m_table->setHorizontalHeaderLabels({ QString("Channel name"), QString("Output bus") });
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);

    const auto& buses = m_project.buses();
    for (int c = 0; c < outCh; ++c) {
        QString defaultName = (c < static_cast<int>(names.size()) && !names[c].isEmpty())
                                  ? names[c]
                                  : QString("Channel %1").arg(c);
        QString name = (c < static_cast<int>(stored.size()) && !stored[c].name.isEmpty())
                           ? stored[c].name
                           : defaultName;
        int bus = (c < static_cast<int>(stored.size())) ? stored[c].busIndex : 0;

        auto* nameEdit = new QLineEdit(name, m_table);
        nameEdit->setFrame(true);
        m_table->setCellWidget(c, 0, nameEdit);

        auto* busCombo = new QComboBox(m_table);
        busCombo->addItem("Master", 0);
        for (int j = 1; j < static_cast<int>(buses.size()); ++j)
            busCombo->addItem(buses[j].name(), j);
        int comboIdx = busCombo->findData(bus);
        if (comboIdx < 0) comboIdx = 0;
        busCombo->setCurrentIndex(comboIdx);
        m_table->setCellWidget(c, 1, busCombo);
    }

    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* createBtn = new QPushButton("Create bus per channel", buttons);
    createBtn->setObjectName("createBusesButton");
    createBtn->setToolTip("Create a separate bus for every channel and assign "
                          "each channel to its own bus");
    connect(createBtn, &QPushButton::clicked, this, &ChannelRoutingDialog::createBusesForChannels);
    buttons->addButton(createBtn, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ChannelRoutingDialog::createBusesForChannels() {
    if (!m_createdBusIndices.empty())
        return;

    int rows = m_table->rowCount();
    for (int c = 0; c < rows; ++c) {
        QString channelName = QString("Channel %1").arg(c);
        if (auto* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(c, 0))) {
            QString text = nameEdit->text().trimmed();
            if (!text.isEmpty())
                channelName = text;
        }

        AudioBus bus;
        QString busName = uniqueBusName(m_project, channelName);
        bus.setName(busName);
        bus.setVolume(1.0f);
        bus.setPan(0.0f);
        bus.setOutputBusIndex(0);
        int newBusIdx = m_project.addBus(std::move(bus));
        m_createdBusIndices.push_back(newBusIdx);

        if (auto* busCombo = qobject_cast<QComboBox*>(m_table->cellWidget(c, 1))) {
            busCombo->addItem(busName, newBusIdx);
            busCombo->setCurrentIndex(busCombo->count() - 1);
        }
    }

    if (auto* createBtn = findChild<QPushButton*>("createBusesButton"))
        createBtn->setEnabled(false);
}

void ChannelRoutingDialog::accept() {
    m_routes.clear();
    for (int c = 0; c < m_table->rowCount(); ++c) {
        Instrument::ChannelRoute route;
        if (auto* nameEdit = qobject_cast<QLineEdit*>(m_table->cellWidget(c, 0)))
            route.name = nameEdit->text().trimmed();
        if (auto* busCombo = qobject_cast<QComboBox*>(m_table->cellWidget(c, 1)))
            route.busIndex = busCombo->currentData().toInt();
        m_routes.push_back(std::move(route));
    }
    QDialog::accept();
}

void ChannelRoutingDialog::reject() {
    // Undo any buses created while the dialog was open. They are always the
    // highest-index buses, so removing them in reverse order is index-stable.
    for (int i = static_cast<int>(m_createdBusIndices.size()) - 1; i >= 0; --i)
        m_project.removeBus(m_createdBusIndices[i]);
    m_createdBusIndices.clear();
    QDialog::reject();
}

void ChannelRoutingDialog::applyTo(Instrument& instrument) const {
    instrument.setMultiChannel(true);
    instrument.setChannelRoutes(m_routes);
}
