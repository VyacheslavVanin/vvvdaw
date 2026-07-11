#include "VST3Instance.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/gui/iplugview.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <vector>
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
    destroyEditor();
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

static bool findAudioProcessorUID(IPluginFactory* factory, const std::string& soPath, TUID outUID) {
    memset(outUID, 0, 16);
    std::ifstream file(soPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    auto sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buf(sz);
    file.read(reinterpret_cast<char*>(buf.data()), sz);

    const char compIID[] = "\xE8\x31\xFF\x31\xF2\xD5\x43\x01\x92\x8E\xBB\xEE\x25\x69\x78\x02";

    for (size_t i = 0; i + 16 <= buf.size(); i += 4) {
        bool allZero = true;
        for (int j = 0; j < 16; ++j) if (buf[i+j] != 0) { allZero = false; break; }
        if (allZero) continue;

        TUID tuid;
        memcpy(tuid, buf.data() + i, 16);

        void* obj = nullptr;
        factory->createInstance(tuid, compIID, &obj);
        if (obj) {
            memcpy(outUID, tuid, 16);
            IPluginBase* base = nullptr;
            ((FUnknown*)obj)->queryInterface(IPluginBase::iid, (void**)&base);
            if (base) base->release();
            return true;
        }
    }
    return false;
}

bool VST3Instance::load(const QString& path) {
    std::string soPath;
    namespace fs = std::filesystem;
    fs::path bundlePath(path.toStdString());

    if (fs::is_directory(bundlePath)) {
        for (auto& sub : fs::recursive_directory_iterator(bundlePath)) {
            if (sub.path().extension() == ".so") {
                soPath = sub.path().string();
                break;
            }
        }
        if (soPath.empty()) return false;
    } else {
        soPath = path.toStdString();
    }

    m_dlHandle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_dlHandle) return false;

    using GetFactoryFunc = IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(m_dlHandle, "GetPluginFactory"));
    if (!getFactory) return false;

    IPluginFactory* rawFactory = getFactory();
    if (!rawFactory) return false;

    Steinberg::IPtr<IPluginFactory> factory;
    factory = Steinberg::owned(rawFactory);

    TUID compUID = {0};
    if (!findAudioProcessorUID(factory.get(), soPath, compUID)) return false;

    IComponent* comp = nullptr;
    factory->createInstance(compUID, IComponent::iid, (void**)&comp);
    if (!comp) return false;
    m_component = Steinberg::owned(comp);

    m_component->initialize(&m_hostApp);

    IAudioProcessor* ap = nullptr;
    m_component->queryInterface(IAudioProcessor::iid, (void**)&ap);
    if (!ap) {
        m_component->terminate();
        m_component = nullptr;
        return false;
    }
    m_audioProcessor = Steinberg::owned(ap);

    std::string stem = fs::path(soPath).parent_path().parent_path().stem().string();
    m_name = QString::fromStdString(stem);
    m_pluginId = QString::fromStdString(stem);

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
    return m_controller != nullptr;
}

void* VST3Instance::createEditor(void* parentWindow) {
    if (!m_controller) return nullptr;
    if (m_editorView) return parentWindow;

    auto* view = m_controller->createView(ViewType::kEditor);
    if (!view) return nullptr;

    view->setFrame(nullptr);

    if (view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) == kResultTrue) {
        m_editorView = view;
        m_editorView->attached(parentWindow, kPlatformTypeX11EmbedWindowID);
        ViewRect rect;
        m_editorView->getSize(&rect);
        return parentWindow;
    }

    view->release();
    return nullptr;
}

void VST3Instance::destroyEditor() {
    if (!m_editorView) return;
    m_editorView->removed();
    m_editorView->release();
    m_editorView = nullptr;
}

void VST3Instance::resizeEditor(int width, int height) {
    if (!m_editorView) return;
    ViewRect rect(0, 0, width, height);
    m_editorView->onSize(&rect);
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
