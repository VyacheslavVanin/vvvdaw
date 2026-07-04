#include <QApplication>
#include <portaudio.h>
#include "core/Settings.h"
#include "audio/AudioEngine.h"
#include "gui/MainWindow.h"
#include "model/Project.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("vvvdaw");
    app.setOrganizationName("vvvdaw");

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qFatal("PortAudio error: %s", Pa_GetErrorText(err));
    }

    Settings settings;
    settings.load();

    AudioEngine audioEngine;
    if (!audioEngine.init(settings)) {
        qWarning("Failed to initialize audio engine");
    } else {
        audioEngine.startStream();
    }

    Project project;
    project.addTrack("Track 1");

    MainWindow window(project, audioEngine, settings);
    window.show();

    int result = app.exec();

    audioEngine.shutdown();
    settings.save();
    Pa_Terminate();
    return result;
}
