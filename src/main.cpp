#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <portaudio.h>
#include <cstdlib>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "gui/MainWindow.h"
#include "gui/StartDialog.h"
#include "model/Project.h"
#include "model/TemplateStore.h"
#include <QMessageBox>

static void crashHandler(int sig) {
    fprintf(stderr, "\n=== SEGFAULT (signal %d) ===\n", sig);
    void* array[64];
    int size = backtrace(array, 64);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    _exit(1);
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crashHandler);
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        if (sessionType.contains("wayland")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("vvvdaw");
    app.setOrganizationName("vvvdaw");

    QString projectFile;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--project" && i + 1 < args.size()) {
            projectFile = args[++i];
        }
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qFatal("PortAudio error: %s", Pa_GetErrorText(err));
    }

    Settings settings;
    settings.load();

    Project project;
    if (!projectFile.isEmpty()) {
        if (project.load(projectFile)) {
            settings.addRecentProject(projectFile);
        } else {
            qWarning("Failed to load project: %s", qPrintable(projectFile));
            project.addTrack("Track 1");
        }
    } else {
        // Show the startup dialog before any audio device is opened, so a
        // declined start quits cleanly without touching the audio stack.
        StartDialog startDialog(settings);
        if (startDialog.exec() != QDialog::Accepted) {
            settings.save();
            return 0;
        }
        switch (startDialog.choice().action) {
        case StartDialog::Action::OpenRecent:
        case StartDialog::Action::Browse: {
            const QString path = startDialog.choice().path;
            if (!path.isEmpty() && project.load(path)) {
                settings.addRecentProject(path);
            } else {
                settings.removeRecentProject(path);
                QMessageBox::warning(nullptr, "vvvdaw", "Failed to open project.");
                project.addTrack("Track 1");
            }
            break;
        }
        case StartDialog::Action::OpenTemplate:
            if (!TemplateStore::loadTemplate(project, startDialog.choice().templateName)) {
                QMessageBox::warning(nullptr, "vvvdaw", "Failed to open template.");
                project.addTrack("Track 1");
            }
            break;
        case StartDialog::Action::Exit:
            settings.save();
            return 0;
        default:
            project.addTrack("Track 1");
        }
    }

    AudioEngine audioEngine;
    if (!audioEngine.init(settings)) {
        qWarning("Failed to initialize audio engine");
    } else {
        audioEngine.startStream();
    }
    project.setSampleRate(audioEngine.sampleRate());

    MainWindow window(project, audioEngine, settings);
    window.show();

    int result = app.exec();

    audioEngine.deactivateAllPlugins();
    audioEngine.shutdown();
    // Destroy plugin instances before MainWindow's PluginManager (lilv world)
    for (auto& t : project.tracks())
        t.pluginChain().clear();
    for (auto& b : project.buses())
        b.pluginChain().clear();
    for (auto& inst : project.instruments()) {
        inst.setSynth(nullptr);
        inst.effects().clear();
    }

    settings.save();
    Pa_Terminate();
    return result;
}
