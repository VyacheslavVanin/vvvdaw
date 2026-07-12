#pragma once
#include "PluginInstance.h"
#include <memory>
#include <vector>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/gui/iplugview.h>
#include "MemoryStream.h"
#include <public.sdk/source/vst/hosting/hostclasses.h>

class HostComponentHandler : public Steinberg::Vst::IComponentHandler {
public:
    void setController(Steinberg::Vst::IEditController* ctrl) { m_controller = ctrl; }

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override {
        if (m_controller)
            m_controller->setParamNormalized(id, value);
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultTrue; }

    DECLARE_FUNKNOWN_METHODS

private:
    Steinberg::Vst::IEditController* m_controller = nullptr;
};

class HostParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    HostParamValueQueue() = default;

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return m_paramId; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return static_cast<Steinberg::int32>(m_points.size()); }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(m_points.size()))
            return Steinberg::kInvalidArgument;
        sampleOffset = m_points[index].first;
        value = m_points[index].second;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value,
                                Steinberg::int32& index) override {
        index = static_cast<Steinberg::int32>(m_points.size());
        m_points.emplace_back(sampleOffset, value);
        return Steinberg::kResultTrue;
    }

    void setParameterId(Steinberg::Vst::ParamID id) { m_paramId = id; }
    void clear() { m_points.clear(); }

    DECLARE_FUNKNOWN_METHODS

private:
    Steinberg::Vst::ParamID m_paramId = 0;
    std::vector<std::pair<Steinberg::int32, Steinberg::Vst::ParamValue>> m_points;
};

class HostParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    HostParameterChanges() = default;

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(m_queues.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(m_queues.size()))
            return nullptr;
        return &m_queues[index];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                   Steinberg::int32& index) override {
        for (Steinberg::int32 i = 0; i < static_cast<Steinberg::int32>(m_queues.size()); ++i) {
            if (m_queues[i].getParameterId() == id) {
                index = i;
                return &m_queues[i];
            }
        }
        index = static_cast<Steinberg::int32>(m_queues.size());
        m_queues.emplace_back();
        m_queues.back().setParameterId(id);
        return &m_queues.back();
    }

    void clear() {
        for (auto& q : m_queues)
            q.clear();
    }

    DECLARE_FUNKNOWN_METHODS

private:
    std::vector<HostParamValueQueue> m_queues;
};

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

    void setParameter(int index, float value) override;
    float getParameter(int index) const override;

    bool hasEditor() const override;
    void* createEditor(void* parentWindow) override;
    void destroyEditor() override;
    void resizeEditor(int width, int height) override;
    bool getEditorSize(int& width, int& height) const override;

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

    HostComponentHandler m_componentHandler;
    HostParameterChanges m_outputParamChanges;

    std::vector<Steinberg::int32> m_inputBusChannels;
    std::vector<Steinberg::int32> m_outputBusChannels;
};
