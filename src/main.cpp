#include <QApplication>
#include <portaudio.h>
#include <cstdlib>
#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "gui/MainWindow.h"
#include "model/Project.h"

int main(int argc, char* argv[]) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        if (sessionType.contains("wayland")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("vvvdaw");
    app.setOrganizationName("vvvdaw");

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qFatal("PortAudio error: %s", Pa_GetErrorText(err));
    }

    Settings settings;
    settings.load();

    int result;
    {
        Project project;
        project.addTrack("Track 1");

        AudioEngine audioEngine;
        if (!audioEngine.init(settings)) {
            qWarning("Failed to initialize audio engine");
        } else {
            audioEngine.startStream();
        }
        project.setSampleRate(audioEngine.sampleRate());

        MainWindow window(project, audioEngine, settings);
        window.show();

        result = app.exec();

        audioEngine.deactivateAllPlugins();
        audioEngine.shutdown();
    }

    settings.save();
    Pa_Terminate();
    return result;
}
