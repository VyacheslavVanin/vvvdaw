#pragma once
#include "PluginInstance.h"
#include <memory>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/gui/iplugview.h>
#include "MemoryStream.h"
#include <public.sdk/source/vst/hosting/hostclasses.h>

class VST3Instance : public PluginInstance {
public:
    VST3Instance();
    ~VST3Instance() override;

    bool load(const QString& path) override;
    bool activate(double sampleRate, int maxBlockSize) override;
    bool deactivate() override;
    bool process(float** inputBuffers, float** outputBuffers,
                 int numSamples, int numChannels) override;

    QString name() const override;
    QString vendor() const override;
    QString pluginId() const override;
    QString filePath() const override;

    bool isActive() const override;
    void setEnabled(bool enabled) override;
    bool isEnabled() const override;

    int latencySamples() const override;
    std::vector<PluginPortInfo> ports() const override;

    bool hasEditor() const override;
    void* createEditor(void* parentWindow) override;
    void destroyEditor() override;
    void resizeEditor(int width, int height) override;

    QJsonObject stateToJson() const override;
    void stateFromJson(const QJsonObject& json) override;

private:
    bool m_enabled = true;
    bool m_active = false;
    double m_sampleRate = 48000;
    int m_maxBlockSize = 512;
    QString m_filePath;
    QString m_name;
    QString m_vendor;
    QString m_pluginId;

    void* m_dlHandle = nullptr;
    Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_audioProcessor;
    Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> m_componentCP;
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> m_controllerCP;

    Steinberg::Vst::HostApplication m_hostApp;
    bool m_separateController = false;
    Steinberg::IPlugView* m_editorView = nullptr;
    Steinberg::IPlugFrame* m_frame = nullptr;
    void* m_frameImpl = nullptr;
};
