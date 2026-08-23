#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TransportPanel.h"
#include "TimelineRuler.h"
#include "MeasureRuler.h"
#include "TempoWidget.h"
#include "TrackPanelWidget.h"
#include "TrackViewWidget.h"
#include "BusPanelWidget.h"
#include "InstrumentPanelWidget.h"
#include "PianoRollWindow.h"
#include "PluginListWidget.h"
#include "PluginWindow.h"
#include "StartDialog.h"
#include "core/UndoStack.h"
#include "core/TimeUtils.h"
#include "commands/TrackCommands.h"
#include "commands/BusCommands.h"
#include "commands/ProjectCommands.h"
#include "commands/EventCommands.h"
#include "commands/MidiCommands.h"
#include "commands/InstrumentCommands.h"
#include "commands/PluginCommands.h"
#include "commands/SnapshotCommand.h"
#include "model/Project.h"
#include "model/Track.h"
#include "model/AudioBus.h"
#include "model/AudioEvent.h"
#include "model/AudioClip.h"
#include "model/Instrument.h"
#include "model/TemplateStore.h"
#include "audio/AudioEngine.h"
#include "audio/DeviceInfo.h"
#include "core/Settings.h"
#include "plugin/PluginInstance.h"
#include "plugin/PluginChain.h"
#include "plugin/LV2Instance.h"

using vvvdaw::TransportState;
#include <QApplication>
#include <QMouseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QJsonArray>
#include <cmath>

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Project", QKeySequence::New);
    auto* openAction = fileMenu->addAction("&Open Project...", QKeySequence::Open);
    auto* saveAction = fileMenu->addAction("&Save Project", QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction("Save &As...", QKeySequence("Ctrl+Shift+S"));
    auto* saveTemplateAction = fileMenu->addAction("Save as &Template...");
    fileMenu->addSeparator();
    auto* settingsAction = fileMenu->addAction("&Settings...");
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("&Quit", QKeySequence::Quit);

    connect(newAction, &QAction::triggered, this, [this] {
        // Reuse the start dialog so a new project can come from a template or
        // a recently opened project.
        StartDialog dialog(m_settings, this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        switch (dialog.choice().action) {
        case StartDialog::Action::OpenRecent:
        case StartDialog::Action::Browse: {
            const QString path = dialog.choice().path;
            if (path.isEmpty()) return;
            Project project;
            if (!project.load(path)) {
                QMessageBox::warning(this, "Error", "Failed to load project.");
                m_settings.removeRecentProject(path);
                return;
            }
            replaceProject(std::move(project));
            m_settings.addRecentProject(path);
            setWindowTitle("vvvdaw - " + QFileInfo(path).absolutePath());
            break;
        }
        case StartDialog::Action::OpenTemplate: {
            Project project;
            if (!TemplateStore::loadTemplate(project, dialog.choice().templateName)) {
                QMessageBox::warning(this, "Error", "Failed to open template.");
                return;
            }
            replaceProject(std::move(project));
            setWindowTitle("vvvdaw - " + m_project.name());
            break;
        }
        case StartDialog::Action::Exit:
        default:
            break; // Cancel: keep the current project as-is.
        }
    });

    connect(openAction, &QAction::triggered, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Open Project",
            QString(), "Project Files (project.json)");
        if (path.isEmpty()) return;

        Project project;
        if (!project.load(path)) {
            QMessageBox::warning(this, "Error", "Failed to load project.");
            return;
        }
        replaceProject(std::move(project));
        m_settings.addRecentProject(path);
        setWindowTitle("vvvdaw - " + QFileInfo(path).absolutePath());
    });

    connect(saveAction, &QAction::triggered, this, [this, saveAsAction] {
        if (m_project.filePath().isEmpty()) {
            saveAsAction->trigger();
            return;
        }
        if (!m_project.save(m_project.filePath())) {
            QMessageBox::warning(this, "Error", "Failed to save project.");
        } else {
            m_settings.addRecentProject(m_project.filePath());
        }
    });

    connect(saveAsAction, &QAction::triggered, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "Choose Project Directory");
        if (dir.isEmpty()) return;

        QString path = dir + "/project.json";
        if (!m_project.save(path)) {
            QMessageBox::warning(this, "Error", "Failed to save project.");
            return;
        }
        m_settings.addRecentProject(path);
        setWindowTitle("vvvdaw - " + dir);
    });

    connect(saveTemplateAction, &QAction::triggered, this, [this] {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Save as Template",
                                             "Template name:", QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        name = TemplateStore::sanitizeName(name);
        if (name.isEmpty()) {
            QMessageBox::warning(this, "Save as Template",
                                 "The template name is not valid.");
            return;
        }
        bool overwrite = TemplateStore::exists(name);
        if (overwrite && QMessageBox::question(
                this, "Overwrite Template",
                QString("Template \"%1\" already exists.\nOverwrite it?").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!TemplateStore::saveTemplate(m_project, name, overwrite)) {
            QMessageBox::warning(this, "Save as Template",
                                 "Failed to save template.");
        }
    });

    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* undoAction = editMenu->addAction("&Undo", QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &MainWindow::performUndo);
    auto* redoAction = editMenu->addAction("&Redo", QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::performRedo);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* resetZoomAction = viewMenu->addAction("Reset &Zoom", QKeySequence("Ctrl+0"));
    connect(resetZoomAction, &QAction::triggered, this, [this] {
        m_zoom = vvvdaw::DefaultZoom;
        syncZoom();
        m_trackContainer->update();
    });

    auto* toggleBusPanelAction = viewMenu->addAction("Show &Bus Panel", QKeySequence("Ctrl+B"));
    toggleBusPanelAction->setCheckable(true);
    toggleBusPanelAction->setChecked(m_settings.busPanelVisible);
    connect(toggleBusPanelAction, &QAction::triggered, this, [this](bool checked) {
        m_settings.busPanelVisible = checked;
        m_busPanel->setVisible(checked);
        m_busPanelGrip->setVisible(checked);
        if (checked)
            m_busPanel->rebuild();
    });

    auto* toggleInstrumentPanelAction = viewMenu->addAction("Show &Instrument Panel", QKeySequence("Ctrl+I"));
    toggleInstrumentPanelAction->setCheckable(true);
    toggleInstrumentPanelAction->setChecked(m_settings.instrumentPanelVisible);
    connect(toggleInstrumentPanelAction, &QAction::triggered, this, [this](bool checked) {
        m_settings.instrumentPanelVisible = checked;
        m_instrumentPanel->setVisible(checked);
        m_instrumentPanelGrip->setVisible(checked);
        if (checked)
            m_instrumentPanel->rebuild();
    });

    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(m_settings, m_engine, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_engine.shutdown();
            m_engine.init(m_settings);
            m_engine.startStream();
            m_project.setSampleRate(m_engine.sampleRate());
        }
    });

    auto* trackMenu = menuBar()->addMenu("&Track");
    auto* addMonoAction = trackMenu->addAction("Add &Mono Track", QKeySequence("Ctrl+T"));
    connect(addMonoAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), 1));
    });
    auto* addStereoAction = trackMenu->addAction("Add &Stereo Track", QKeySequence("Ctrl+M"));
    connect(addStereoAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), 2));
    });
    auto* addMidiAction = trackMenu->addAction("Add &MIDI Track", QKeySequence("Ctrl+Shift+T"));
    connect(addMidiAction, &QAction::triggered, this, [this] {
        executeCommand(std::make_unique<AddTrackCommand>(m_project, static_cast<int>(m_project.tracks().size()), Track::Type::Midi));
    });

    auto* deleteTrackAction = trackMenu->addAction("&Delete Track");
    connect(deleteTrackAction, &QAction::triggered, this, [this] {
        for (size_t i = 0; i < m_project.tracks().size(); ++i) {
            if (m_trackRows[i].panel->hasFocus() || m_trackRows[i].view->hasFocus()) {
                int idx = static_cast<int>(i);
                std::vector<PluginInstance*> plugins;
                auto& chain = m_project.tracks()[idx].pluginChain();
                for (int j = 0; j < chain.count(); ++j)
                    plugins.push_back(chain.plugin(j));
                closePluginWindowsFor(plugins);
                executeCommand(std::make_unique<RemoveTrackCommand>(m_project, idx, &m_pluginManager));
                return;
            }
        }
        for (size_t i = m_project.tracks().size(); i > 0; --i) {
            if (!m_trackRows[i - 1].view->selectedEventIds().empty()) {
                int idx = static_cast<int>(i - 1);
                std::vector<PluginInstance*> plugins;
                auto& chain = m_project.tracks()[idx].pluginChain();
                for (int j = 0; j < chain.count(); ++j)
                    plugins.push_back(chain.plugin(j));
                closePluginWindowsFor(plugins);
                executeCommand(std::make_unique<RemoveTrackCommand>(m_project, idx, &m_pluginManager));
                return;
            }
        }
    });

    trackMenu->addSeparator();

    auto* deleteAction = trackMenu->addAction("&Delete Event");
    connect(deleteAction, &QAction::triggered, this, [this] {
        for (auto& row : m_trackRows) {
            if (row.view->hasFocus()) {
                row.view->deleteSelectedEvent();
                return;
            }
        }
        for (auto& row : m_trackRows) {
            if (!row.view->selectedEventIds().empty()) {
                row.view->deleteSelectedEvent();
                return;
            }
        }
    });
}
