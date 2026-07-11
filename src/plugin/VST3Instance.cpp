#include "VST3Instance.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/gui/iplugview.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <dlfcn.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;
using vvvdaw::StateStream;

StateStream::StateStream() = default;
StateStream::~StateStream() = default;

VST3Instance::VST3Instance() = default;

VST3Instance::~VST3Instance() {
    deactivate();
    if (m_controllerCP && m_componentCP) {
        m_controllerCP->disconnect(m_componentCP);
        m_componentCP->disconnect(m_controllerCP);
    }
    m_controller = nullptr;
    m_component = nullptr;
    m_audioProcessor = nullptr;
    if (m_dlHandle) dlclose(m_dlHandle);
}

bool VST3Instance::load(const QString& path) {
    m_dlHandle = dlopen(path.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!m_dlHandle) return false;

    using GetFactoryFunc = IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(m_dlHandle, "GetPluginFactory"));
    if (!getFactory) return false;

    IPluginFactory* rawFactory = getFactory();
    if (!rawFactory) return false;

    Steinberg::IPtr<IPluginFactory> factory;
    factory = Steinberg::owned(rawFactory);

    for (int32 i = 0; i < factory->countClasses(); ++i) {
        PClassInfo ci;
        factory->getClassInfo(i, &ci);
        if (std::string(ci.category) == kVstAudioEffectClass) {
            IComponent* comp = nullptr;
            factory->createInstance(ci.cid, IComponent::iid, (void**)&comp);
            if (!comp) continue;
            m_component = Steinberg::owned(comp);

            m_component->initialize(&m_hostApp);

            IAudioProcessor* ap = nullptr;
            m_component->queryInterface(IAudioProcessor::iid, (void**)&ap);
            if (!ap) {
                m_component->terminate();
                m_component = nullptr;
                continue;
            }
            m_audioProcessor = Steinberg::owned(ap);

            m_name = QString::fromUtf8(ci.name);
            m_pluginId = QString::fromUtf8(ci.name);

            Steinberg::TUID controllerTUID = {0};
            if (m_component->getControllerClassId(controllerTUID) == kResultTrue &&
                Steinberg::FUID(controllerTUID).isValid()) {
                IEditController* ctrl = nullptr;
                factory->createInstance(controllerTUID, IEditController::iid, (void**)&ctrl);
                if (ctrl) m_controller = Steinberg::owned(ctrl);
                if (m_controller) {
                    m_controller->initialize(&m_hostApp);
                    m_separateController = true;
                }
            }

            if (!m_controller) {
                IEditController* ctrl = nullptr;
                m_component->queryInterface(IEditController::iid, (void**)&ctrl);
                if (ctrl) m_controller = Steinberg::owned(ctrl);
            }

            if (m_component) {
                IConnectionPoint* cp = nullptr;
                m_component->queryInterface(IConnectionPoint::iid, (void**)&cp);
                if (cp) m_componentCP = Steinberg::owned(cp);
            }
            if (m_controller) {
                IConnectionPoint* cp = nullptr;
                m_controller->queryInterface(IConnectionPoint::iid, (void**)&cp);
                if (cp) m_controllerCP = Steinberg::owned(cp);
            }

            if (m_componentCP && m_controllerCP) {
                m_componentCP->connect(m_controllerCP);
                m_controllerCP->connect(m_componentCP);
            }

            if (m_controller) {
                StateStream stream;
                if (m_component->getState(&stream) == kResultTrue) {
                    stream.reset();
                    m_controller->setComponentState(&stream);
                }
            }

            m_filePath = path;
            return true;
        }
    }
    return false;
}

bool VST3Instance::activate(double sampleRate, int maxBlockSize) {
    if (!m_component || !m_audioProcessor) return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;

    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;

    if (m_audioProcessor->setupProcessing(setup) != kResultTrue) return false;

    m_component->setActive(true);
    m_audioProcessor->setProcessing(true);
    m_active = true;
    return true;
}

bool VST3Instance::deactivate() {
    if (!m_active) return true;
    if (m_audioProcessor) m_audioProcessor->setProcessing(false);
    if (m_component) m_component->setActive(false);
    m_active = false;
    return true;
}

bool VST3Instance::process(float** inputBuffers, float** outputBuffers,
                           int numSamples, int numChannels) {
    if (!m_active || !m_audioProcessor || !m_enabled) {
        if (outputBuffers && inputBuffers) {
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy(outputBuffers[ch], inputBuffers[ch], numSamples * sizeof(float));
        }
        return true;
    }

    AudioBusBuffers inBus, outBus;
    inBus.numChannels = numChannels;
    inBus.silenceFlags = 0;
    inBus.channelBuffers32 = inputBuffers;

    outBus.numChannels = numChannels;
    outBus.silenceFlags = 0;
    outBus.channelBuffers32 = outputBuffers;

    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = numSamples;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    return m_audioProcessor->process(data) == kResultTrue;
}

QString VST3Instance::name() const { return m_name; }
QString VST3Instance::vendor() const { return m_vendor; }
QString VST3Instance::pluginId() const { return m_pluginId; }
QString VST3Instance::filePath() const { return m_filePath; }
bool VST3Instance::isActive() const { return m_active; }
void VST3Instance::setEnabled(bool enabled) { m_enabled = enabled; }
bool VST3Instance::isEnabled() const { return m_enabled; }

int VST3Instance::latencySamples() const {
    if (m_audioProcessor) return m_audioProcessor->getLatencySamples();
    return 0;
}

std::vector<PluginPortInfo> VST3Instance::ports() const {
    std::vector<PluginPortInfo> result;
    if (!m_component) return result;

    auto count = m_component->getBusCount(kAudio, kInput);
    for (int32 i = 0; i < count; ++i) {
        PluginPortInfo pi;
        pi.type = PluginPortInfo::Type::Audio;
        pi.direction = PluginPortInfo::Direction::Input;
        pi.name = QString("Audio In %1").arg(i);
        pi.index = i;
        result.push_back(pi);
    }

    count = m_component->getBusCount(kAudio, kOutput);
    for (int32 i = 0; i < count; ++i) {
        PluginPortInfo pi;
        pi.type = PluginPortInfo::Type::Audio;
        pi.direction = PluginPortInfo::Direction::Output;
        pi.name = QString("Audio Out %1").arg(i);
        pi.index = i;
        result.push_back(pi);
    }

    if (m_controller) {
        int32 paramCount = m_controller->getParameterCount();
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo pi;
            if (m_controller->getParameterInfo(i, pi) == kResultTrue) {
                PluginPortInfo portInfo;
                portInfo.type = PluginPortInfo::Type::Control;
                portInfo.direction = PluginPortInfo::Direction::Input;
                portInfo.name = QString::fromUtf16(pi.title);
                portInfo.index = i;
                portInfo.defaultValue = static_cast<float>(pi.defaultNormalizedValue);
                portInfo.minValue = 0.0f;
                portInfo.maxValue = 1.0f;
                result.push_back(portInfo);
            }
        }
    }

    return result;
}

bool VST3Instance::hasEditor() const {
    if (!m_controller) return false;
    auto* view = m_controller->createView(ViewType::kEditor);
    if (!view) return false;
    view->release();
    return true;
}

void* VST3Instance::createEditor(void* parentWindow) {
    if (!m_controller) return nullptr;

    auto* view = m_controller->createView(ViewType::kEditor);
    if (!view) return nullptr;

    view->setFrame(nullptr);

    if (view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) == kResultTrue) {
        view->attached(parentWindow, kPlatformTypeX11EmbedWindowID);
        ViewRect rect;
        view->getSize(&rect);
        return parentWindow;
    }

    view->release();
    return nullptr;
}

void VST3Instance::destroyEditor() {
    if (!m_controller) return;
    auto* view = m_controller->createView(ViewType::kEditor);
    if (view) {
        view->removed();
        view->release();
    }
}

void VST3Instance::resizeEditor(int width, int height) {
    if (!m_controller) return;
    auto* view = m_controller->createView(ViewType::kEditor);
    if (view) {
        ViewRect rect(0, 0, width, height);
        view->onSize(&rect);
        view->release();
    }
}

QJsonObject VST3Instance::stateToJson() const {
    QJsonObject json;
    json["type"] = "vst3";
    json["path"] = m_filePath;
    json["pluginId"] = m_pluginId;
    json["enabled"] = m_enabled;

    if (m_component) {
        StateStream stream;
        if (m_component->getState(&stream) == kResultTrue) {
            int64 dataSize = 0;
            stream.tell(&dataSize);
            stream.reset();
            std::vector<char> data(dataSize);
            int32 bytesRead = 0;
            stream.read(data.data(), static_cast<int32>(dataSize), &bytesRead);

            QByteArray ba(data.data(), bytesRead);
            json["state"] = QString::fromLatin1(ba.toBase64());
        }
    }

    return json;
}

void VST3Instance::stateFromJson(const QJsonObject& json) {
    if (json.contains("enabled"))
        m_enabled = json["enabled"].toBool(true);

    if (json.contains("state") && m_component) {
        QByteArray ba = QByteArray::fromBase64(json["state"].toString().toLatin1());
        StateStream stream;
        int32 written = 0;
        stream.write(ba.data(), static_cast<int32>(ba.size()), &written);
        stream.reset();
        m_component->setState(&stream);

        if (m_controller && m_separateController) {
            stream.reset();
            m_controller->setComponentState(&stream);
        }
    }
}
