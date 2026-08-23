#pragma once
// Shared helpers for the GUI test suites. Kept in an anonymous namespace so
// each test translation unit gets its own copy without extra linkage.

#include <QImage>
#include <QColor>
#include <QComboBox>
#include <QWidget>
#include <QString>
#include <QTemporaryDir>
#include <QJsonObject>
#include <memory>
#include <vector>
#include <portaudio.h>

#include "model/TemplateStore.h"
#include "plugin/PluginInstance.h"

namespace {

QComboBox* findComboContaining(QWidget* parent, const QString& text) {
    const auto combos = parent->findChildren<QComboBox*>();
    for (QComboBox* cb : combos)
        for (int i = 0; i < cb->count(); ++i)
            if (cb->itemText(i).contains(text))
                return cb;
    return nullptr;
}

// True if any pixel in the rectangle has the waveform color (high blue, as in
// the default #88ccff waveform). Used so the tests do not depend on the exact
// rasterization row of a 1-px polyline.
bool regionHasWaveform(const QImage& img, int x0, int x1, int y0, int y1,
                       int minBlue = 200) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (img.pixelColor(x, y).blue() > minBlue)
                return true;
    return false;
}

// True if no pixel in the rectangle has the waveform color.
bool regionIsBackground(const QImage& img, int x0, int x1, int y0, int y1,
                        int maxBlue = 100) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (img.pixelColor(x, y).blue() > maxBlue)
                return false;
    return true;
}

// True if any pixel in the rectangle has the crossfade color (#ff6600 orange,
// distinct from the blue event borders and the amber selection highlight).
bool regionHasCrossfade(const QImage& img, int x0, int x1, int y0, int y1) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 240 && c.green() < 160 && c.blue() < 80)
                return true;
        }
    return false;
}

// Minimal instrument plugin stub with a configurable output channel count,
// used to exercise the channel routing UI without loading a real plugin.
class StubSynth : public PluginInstance {
public:
    int channels = 2;

    int audioOutputChannels() const override { return channels; }
    std::vector<QString> audioOutputNames() const override {
        return { "Kick", "Snare", "HiHat" };
    }

    bool load(const QString&) override { return true; }
    bool activate(double, int) override { return true; }
    bool deactivate() override { return true; }
    bool process(float**, float**, int, int, const MidiBuffer*) override { return true; }
    QString name() const override { return "Stub"; }
    QString vendor() const override { return "Test"; }
    QString pluginId() const override { return "stub"; }
    QString filePath() const override { return "stub"; }
    bool isActive() const override { return true; }
    void setEnabled(bool) override {}
    bool isEnabled() const override { return true; }
    int latencySamples() const override { return 0; }
    std::vector<PluginPortInfo> ports() const override { return {}; }
    void setParameter(int, float) override {}
    float getParameter(int) const override { return 0.0f; }
    bool hasEditor() const override { return false; }
    void* createEditor(void*) override { return nullptr; }
    void destroyEditor() override {}
    void resizeEditor(int, int) override {}
    bool getEditorSize(int&, int&) const override { return false; }
    QJsonObject stateToJson() const override { return {}; }
    void stateFromJson(const QJsonObject&) override {}
};

// RAII test environment: initializes PortAudio (for device enumeration inside
// rebuildTracks) and redirects the template store into a temporary directory.
// init() returns false when PortAudio is unavailable so the caller can QSKIP.
class GuiTestEnv {
public:
    bool init() {
        if (Pa_Initialize() != paNoError)
            return false;
        m_tmpDir = new QTemporaryDir;
        if (!m_tmpDir->isValid()) {
            Pa_Terminate();
            delete m_tmpDir;
            m_tmpDir = nullptr;
            return false;
        }
        TemplateStore::setTemplatesDirOverride(m_tmpDir->path());
        TemplateStore::ensureBuiltInTemplates();
        return true;
    }

    void cleanup() {
        TemplateStore::setTemplatesDirOverride("");
        delete m_tmpDir;
        m_tmpDir = nullptr;
        Pa_Terminate();
    }

    QString path() const { return m_tmpDir ? m_tmpDir->path() : QString(); }
    QString filePath(const QString& name) const {
        return m_tmpDir ? m_tmpDir->filePath(name) : QString();
    }

private:
    QTemporaryDir* m_tmpDir = nullptr;
};

} // namespace