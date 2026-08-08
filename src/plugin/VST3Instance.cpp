#include "VST3Instance.h"
#include "VST3Scan.h"
#include "SigGuard.h"
#include "PluginAudioUtils.h"
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/gui/iplugview.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <dlfcn.h>
#include <filesystem>
#include <vector>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;
using vvvdaw::StateStream;

#include <QTimer>
#include <QSocketNotifier>
#include <csignal>
#include <csetjmp>
#include <unordered_map>

namespace {

class PluginFrame : public IPlugFrame, public Linux::IRunLoop {
public:
    void setHostWindow(QWidget* w) { m_window = w; }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (m_window && newSize) {
            m_window->resize(newSize->right - newSize->left, newSize->bottom - newSize->top);
        }
        return kResultTrue;
    }

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override {
        if (m_fdNotifiers.count(fd)) return kResultFalse;
        auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, m_window);
        QObject::connect(notifier, &QSocketNotifier::activated, [handler](int fd) {
            handler->onFDIsSet(fd);
        });
        m_fdNotifiers[fd] = notifier;
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
        for (auto it = m_fdNotifiers.begin(); it != m_fdNotifiers.end(); ++it) {
            it->second->deleteLater();
            m_fdNotifiers.erase(it);
            return kResultTrue;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval ms) override {
        for (auto& [h, t] : m_timers)
            if (h == handler) return kResultFalse;
        auto* timer = new QTimer(m_window);
        QObject::connect(timer, &QTimer::timeout, [handler]() { handler->onTimer(); });
        timer->start(static_cast<int>(ms));
        m_timers.push_back({handler, timer});
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
        for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
            if (it->first == handler) {
                it->second->stop();
                it->second->deleteLater();
                m_timers.erase(it);
                return kResultTrue;
            }
        }
        return kResultFalse;
    }

    DECLARE_FUNKNOWN_METHODS

private:
    QWidget* m_window = nullptr;
    std::unordered_map<int, QSocketNotifier*> m_fdNotifiers;
    std::vector<std::pair<Linux::ITimerHandler*, QTimer*>> m_timers;
};

tresult PLUGIN_API PluginFrame::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid)) {
        *obj = static_cast<IPlugFrame*>(this);
        addRef();
        return kResultTrue;
    }
    if (FUnknownPrivate::iidEqual(_iid, Linux::IRunLoop::iid)) {
        *obj = static_cast<Linux::IRunLoop*>(this);
        addRef();
        return kResultTrue;
    }
    *obj = nullptr;
    return kResultFalse;
}

uint32 PLUGIN_API PluginFrame::addRef() { return 1; }
uint32 PLUGIN_API PluginFrame::release() { return 0; }

} // anonymous namespace

// HostComponentHandler method implementations
tresult PLUGIN_API HostComponentHandler::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(_iid, IComponentHandler::iid)) {
        *obj = static_cast<IComponentHandler*>(this);
        addRef();
        return kResultTrue;
    }
    *obj = nullptr;
    return kResultFalse;
}
uint32 PLUGIN_API HostComponentHandler::addRef() { return 1; }
uint32 PLUGIN_API HostComponentHandler::release() { return 0; }

tresult PLUGIN_API HostComponentHandler::performEdit(ParamID id, ParamValue value) {
    if (m_instance)
        m_instance->handlePerformEdit(id, value);
    if (m_instance)
        m_instance->queueInputParamChange(id, value);
    return kResultTrue;
}

// HostParamValueQueue method implementations
tresult PLUGIN_API HostParamValueQueue::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(_iid, IParamValueQueue::iid)) {
        *obj = static_cast<IParamValueQueue*>(this);
        addRef();
        return kResultTrue;
    }
    *obj = nullptr;
    return kResultFalse;
}
uint32 PLUGIN_API HostParamValueQueue::addRef() { return 1; }
uint32 PLUGIN_API HostParamValueQueue::release() { return 0; }

// HostParameterChanges method implementations
tresult PLUGIN_API HostParameterChanges::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(_iid, IParameterChanges::iid)) {
        *obj = static_cast<IParameterChanges*>(this);
        addRef();
        return kResultTrue;
    }
    *obj = nullptr;
    return kResultFalse;
}
uint32 PLUGIN_API HostParameterChanges::addRef() { return 1; }
uint32 PLUGIN_API HostParameterChanges::release() { return 0; }

// HostEventList method implementations
tresult PLUGIN_API HostEventList::queryInterface(const TUID _iid, void** obj) {
    if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
        FUnknownPrivate::iidEqual(_iid, IEventList::iid)) {
        *obj = static_cast<IEventList*>(this);
        addRef();
        return kResultTrue;
    }
    *obj = nullptr;
    return kResultFalse;
}
uint32 PLUGIN_API HostEventList::addRef() { return 1; }
uint32 PLUGIN_API HostEventList::release() { return 0; }

// VST3Instance

void VST3Instance::queueInputParamChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    std::lock_guard<std::mutex> lock(m_paramMutex);
    Steinberg::int32 idx;
    auto* q = m_inputParamChanges.addParameterData(id, idx);
    Steinberg::int32 pointIdx;
    q->addPoint(0, value, pointIdx);
}

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
    m_audioProcessor = nullptr;
    m_controller = nullptr;
    m_component = nullptr;
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

    // UID discovery: binary memory-scan first (ABI-independent, works for
    // DPF-based plugins), then crash-guarded getClassInfo fallback for plugins
    // that store their GUID as instruction immediates (e.g. MT-PowerDrumKit).
    TUID compUID = {0};
    if (!VST3Scan::findComponentUID(factory.get(), soPath, compUID)) return false;

    IComponent* comp = nullptr;
    factory->createInstance(compUID, IComponent::iid, (void**)&comp);
    if (!comp) return false;

    std::string stem = fs::path(soPath).parent_path().parent_path().parent_path().stem().string();
    m_name = QString::fromStdString(stem);
    m_pluginId = QString::fromStdString(stem);

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

    if (m_controller) {
        m_componentHandler.setController(m_controller.get());
        m_componentHandler.setInstance(this);
        m_controller->setComponentHandler(&m_componentHandler);
    }

    if (m_component) {
        int32 nIn = m_component->getBusCount(kAudio, kInput);
        for (int32 i = 0; i < nIn; ++i)
            m_component->activateBus(kAudio, kInput, i, true);
        int32 nOut = m_component->getBusCount(kAudio, kOutput);
        for (int32 i = 0; i < nOut; ++i)
            m_component->activateBus(kAudio, kOutput, i, true);

        int32 nEventIn = m_component->getBusCount(kEvent, kInput);
        m_isInstrument = (nEventIn > 0);
        for (int32 i = 0; i < nEventIn; ++i)
            m_component->activateBus(kEvent, kInput, i, true);
        int32 nEventOut = m_component->getBusCount(kEvent, kOutput);
        for (int32 i = 0; i < nEventOut; ++i)
            m_component->activateBus(kEvent, kOutput, i, true);

        m_inputBusChannels.clear();
        for (int32 i = 0; i < nIn; ++i) {
            BusInfo bi{};
            if (m_component->getBusInfo(kAudio, kInput, i, bi) == kResultTrue)
                m_inputBusChannels.push_back(bi.channelCount);
            else
                m_inputBusChannels.push_back(2);
        }
        m_outputBusChannels.clear();
        for (int32 i = 0; i < nOut; ++i) {
            BusInfo bi{};
            if (m_component->getBusInfo(kAudio, kOutput, i, bi) == kResultTrue)
                m_outputBusChannels.push_back(bi.channelCount);
            else
                m_outputBusChannels.push_back(2);
        }
    }

    m_filePath = path;
    return true;
}

bool VST3Instance::activate(double sampleRate, int maxBlockSize) {
    if (!m_component || !m_audioProcessor) return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_monoScratch.resize(static_cast<size_t>(maxBlockSize));

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
                           int numSamples, int numChannels, const MidiBuffer* midi) {
    if (bypassPassthrough(m_active && m_audioProcessor && m_enabled,
                          inputBuffers, outputBuffers, numSamples, numChannels))
        return true;

    int32 numInBuses = m_component ? m_component->getBusCount(kAudio, kInput) : 1;
    int32 numOutBuses = m_component ? m_component->getBusCount(kAudio, kOutput) : 1;
    if (numInBuses < 1) numInBuses = 1;
    if (numOutBuses < 1) numOutBuses = 1;

    std::vector<AudioBusBuffers> inBuses(numInBuses);
    float* monoInChannels[1] = { m_monoScratch.data() };
    for (int32 i = 0; i < numInBuses; ++i) {
        int32 busChannels = (i < static_cast<int32>(m_inputBusChannels.size()))
                                ? m_inputBusChannels[i] : numChannels;
        inBuses[i].numChannels = std::min(busChannels, numChannels);
        inBuses[i].silenceFlags = 0;
        if (busChannels == 1 && numChannels >= 2 && inputBuffers) {
            if (m_monoScratch.size() < static_cast<size_t>(numSamples))
                m_monoScratch.resize(static_cast<size_t>(numSamples));
            foldStereoToMono(m_monoScratch.data(), inputBuffers, numSamples);
            inBuses[i].channelBuffers32 = monoInChannels;
        } else {
            inBuses[i].channelBuffers32 = inputBuffers;
        }
    }

    std::vector<AudioBusBuffers> outBuses(numOutBuses);
    for (int32 i = 0; i < numOutBuses; ++i) {
        int32 busChannels = (i < static_cast<int32>(m_outputBusChannels.size()))
                                ? m_outputBusChannels[i] : numChannels;
        outBuses[i].numChannels = std::min(busChannels, numChannels);
        outBuses[i].silenceFlags = 0;
        outBuses[i].channelBuffers32 = outputBuffers;
    }

    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = numSamples;
    data.numInputs = numInBuses;
    data.numOutputs = numOutBuses;
    data.inputs = inBuses.data();
    data.outputs = outBuses.data();
    data.inputParameterChanges = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        if (m_inputParamChanges.getParameterCount() > 0)
            data.inputParameterChanges = &m_inputParamChanges;
    }
    data.outputParameterChanges = &m_outputParamChanges;
    m_outputParamChanges.clear();
    m_eventList.setFromMidi(midi);
    data.inputEvents = midi && !midi->empty() ? &m_eventList : nullptr;
    data.outputEvents = nullptr;
    data.processContext = nullptr;

    tresult result = m_audioProcessor->process(data);

    // Mono plugin: duplicate the single output channel so downstream mixing
    // sees a centered stereo signal instead of a hard-panned-left one.
    bool monoOut = m_outputBusChannels.size() == 1 && m_outputBusChannels[0] == 1;
    if (monoOut && result == kResultTrue && outputBuffers && numChannels >= 2)
        duplicateMonoToStereo(outputBuffers[0], outputBuffers, numSamples);

    {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        m_inputParamChanges.clear();
    }

    if (result == kResultTrue && m_controller) {
        for (int32 i = 0; i < m_outputParamChanges.getParameterCount(); ++i) {
            auto* queue = m_outputParamChanges.getParameterData(i);
            if (queue && queue->getPointCount() > 0) {
                int32 offset;
                ParamValue value;
                queue->getPoint(queue->getPointCount() - 1, offset, value);
                m_controller->setParamNormalized(queue->getParameterId(), value);
            }
        }
    }

    return result == kResultTrue;
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

void VST3Instance::setParameter(int index, float value) {
    if (!m_controller) return;
    m_controller->setParamNormalized(index, qBound(0.0f, value, 1.0f));
    if (m_paramValueCallback)
        m_paramValueCallback(index, value);
}

float VST3Instance::getParameter(int index) const {
    if (!m_controller) return 0.0f;
    return static_cast<float>(m_controller->getParamNormalized(index));
}

void VST3Instance::handlePerformEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    if (m_paramChangeCallback) {
        float oldValue = m_controller ? static_cast<float>(m_controller->getParamNormalized(id)) : 0.0f;
        m_paramChangeCallback(static_cast<int>(id), oldValue, static_cast<float>(value));
    } else {
        if (m_controller)
            m_controller->setParamNormalized(id, value);
    }
}

bool VST3Instance::hasEditor() const {
    return m_controller != nullptr;
}

void* VST3Instance::createEditor(void* parentWindow) {
    if (m_editorCrashed) {
        qWarning() << m_name << ": native editor disabled after previous crash";
        return nullptr;
    }
    if (!m_controller) { qWarning() << m_name << ": no controller"; return nullptr; }
    if (m_editorView) return parentWindow;

    auto* parentWidget = reinterpret_cast<QWidget*>(parentWindow);
    auto x11WindowId = reinterpret_cast<void*>(parentWidget->winId());

    Steinberg::IPlugView* view = nullptr;

    bool ok = runSigGuarded([&] {
        view = m_controller->createView(ViewType::kEditor);
        if (!view) qWarning() << m_name << ": createView returned nullptr";
    });

    if (!ok) {
        qWarning() << m_name << ": editor crashed (SEGV) in createView — disabling native editor";
        m_editorCrashed = true;
        return nullptr;
    }
    if (!view) return nullptr;

    auto x11support = view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID);
    qInfo() << m_name << ": isPlatformTypeSupported(X11) =" << x11support;

    if (x11support == kResultTrue) {
        bool ok = runSigGuarded([&] {
            if (!m_frame) {
                auto* frame = new PluginFrame();
                frame->setHostWindow(parentWidget);
                m_frameImpl = frame;
                m_frame = frame;
            }
            m_frame->addRef();
            view->setFrame(m_frame);
            m_frame->release();
            m_editorView = view;
            m_editorView->attached(x11WindowId, kPlatformTypeX11EmbedWindowID);
            ViewRect rect;
            m_editorView->getSize(&rect);
        });

        if (!ok) {
            qWarning() << m_name << ": editor crashed (SEGV) during attach — disabling native editor";
            m_editorCrashed = true;
            // The view may be partially initialized; do not touch it.
            m_editorView = nullptr;
            if (m_frameImpl) {
                delete static_cast<PluginFrame*>(m_frameImpl);
                m_frameImpl = nullptr;
                m_frame = nullptr;
            }
            return nullptr;
        }
        return parentWindow;
    }

    qWarning() << m_name << ": X11 not supported, releasing view";
    view->release();
    return nullptr;
}

void VST3Instance::destroyEditor() {
    if (!m_editorView) return;
    m_editorView->removed();
    m_editorView->release();
    m_editorView = nullptr;
    if (m_frameImpl) {
        delete static_cast<PluginFrame*>(m_frameImpl);
        m_frameImpl = nullptr;
        m_frame = nullptr;
    }
}

void VST3Instance::resizeEditor(int width, int height) {
    if (!m_editorView) return;
    ViewRect rect(0, 0, width, height);
    m_editorView->onSize(&rect);
}

bool VST3Instance::getEditorSize(int& width, int& height) const {
    if (!m_editorView) return false;
    ViewRect rect;
    if (m_editorView->getSize(&rect) == kResultTrue) {
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        return width > 0 && height > 0;
    }
    return false;
}

QJsonObject VST3Instance::stateToJson() const {
    QJsonObject json;
    writeIdentityToJson(json, "vst3");

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
    readIdentityFromJson(json);

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
