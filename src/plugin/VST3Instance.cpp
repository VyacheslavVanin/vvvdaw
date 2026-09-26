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

// Enables every audio and event bus on the component. Some plugins expect this
// before setupProcessing(); JUCE's VST3 wrapper additionally requires it after
// setupProcessing() so its host-bus map records the buses as active.
void activateComponentBuses(IComponent* component) {
    if (!component) return;
    const MediaType types[] = {kAudio, kEvent};
    for (MediaType type : types) {
        int32 nIn = component->getBusCount(type, kInput);
        for (int32 i = 0; i < nIn; ++i)
            component->activateBus(type, kInput, i, true);
        int32 nOut = component->getBusCount(type, kOutput);
        for (int32 i = 0; i < nOut; ++i)
            component->activateBus(type, kOutput, i, true);
    }
}

// Per-bus view of the layout metadata, with fallbacks for missing entries.
struct InputBusInfo {
    int32 channels;
    bool sidechain;
    int offset;
};

InputBusInfo inputBusInfo(const VST3Instance::InputBusLayout& layout, int32 index,
                          int fallbackChannels) {
    InputBusInfo info{fallbackChannels, false, -1};
    if (layout.busChannels && index < static_cast<int32>(layout.busChannels->size()))
        info.channels = (*layout.busChannels)[index];
    if (layout.busIsSidechain && index < static_cast<int32>(layout.busIsSidechain->size()))
        info.sidechain = (*layout.busIsSidechain)[index];
    if (layout.busSidechainOffset && index < static_cast<int32>(layout.busSidechainOffset->size()))
        info.offset = (*layout.busSidechainOffset)[index];
    return info;
}

// Whether an input bus wider than the host can be fed by repeating the host
// channels (a narrower layout is rejected by some plugins).
bool canExpandInputBus(int32 busChannels, float** inputBuffers, int numChannels,
                       int expandBase, const VST3Instance::InputBusLayout& layout) {
    if (!inputBuffers || numChannels <= 0 || busChannels <= numChannels || !layout.expandPtrs)
        return false;
    return expandBase + busChannels <= static_cast<int32>(layout.expandPtrs->size());
}

// A sidechain (kAux) input bus is fed from the host scratch the engine filled
// with the key signal.
void configureSidechainInputBus(AudioBusBuffers& bus, int32 busChannels, int offset,
                                int sidechainChannelCount,
                                std::vector<float*>* sidechainPtrs) {
    const int available = (offset >= 0) ? sidechainChannelCount - offset : 0;
    bus.silenceFlags = 0;
    bus.numChannels = std::min<int32>(busChannels, std::max(0, available));
    bus.channelBuffers32 = (offset >= 0 && available > 0 && sidechainPtrs)
                               ? sidechainPtrs->data() + offset
                               : nullptr;
}

// Fills a main (non-sidechain) input bus: repeat the host channels if the bus is
// wider, fold a stereo host into a mono bus, or hand the host buffers through.
void configureMainInputBus(AudioBusBuffers& bus, int32 busChannels, float** inputBuffers,
                           int numSamples, int numChannels, int expandBase, int index,
                           const VST3Instance::InputBusLayout& layout) {
    bus.silenceFlags = 0;
    if (canExpandInputBus(busChannels, inputBuffers, numChannels, expandBase, layout)) {
        expandChannelsToBus(inputBuffers, numChannels,
                            layout.expandPtrs->data() + expandBase, busChannels, numSamples);
        bus.numChannels = busChannels;
        bus.channelBuffers32 = layout.expandPtrs->data() + expandBase;
        return;
    }
    bus.numChannels = std::min(busChannels, numChannels);
    if (busChannels != 1 || numChannels < 2 || !inputBuffers ||
        !layout.monoScratch || !layout.monoFoldPtrs) {
        bus.channelBuffers32 = inputBuffers;
        return;
    }
    // Mono plugin on a stereo host: fold L/R into the mono scratch. The
    // channel-pointer slot must live in caller-owned storage so it stays valid
    // for the whole process() call; a stack-local array here used to dangle and
    // made ZamEQ2 fault reading inputs[0].
    if (layout.monoScratch->size() < static_cast<size_t>(numSamples))
        layout.monoScratch->resize(static_cast<size_t>(numSamples));
    foldStereoToMono(layout.monoScratch->data(), inputBuffers, numSamples);
    (*layout.monoFoldPtrs)[static_cast<size_t>(index)] = layout.monoScratch->data();
    bus.channelBuffers32 = layout.monoFoldPtrs->data() + index;
}

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
        activateComponentBuses(m_component.get());

        int32 nIn = m_component->getBusCount(kAudio, kInput);
        int32 nOut = m_component->getBusCount(kAudio, kOutput);
        int32 nEventIn = m_component->getBusCount(kEvent, kInput);
        m_isInstrument = (nEventIn > 0);

        m_inputBusChannels.clear();
        m_inputBusIsSidechain.clear();
        m_inputBusSidechainOffset.clear();
        int sidechainOffset = 0;
        for (int32 i = 0; i < nIn; ++i) {
            BusInfo bi{};
            bool ok = m_component->getBusInfo(kAudio, kInput, i, bi) == kResultTrue;
            int channels = ok ? bi.channelCount : 2;
            bool sidechain = ok && bi.busType == kAux;
            m_inputBusChannels.push_back(channels);
            m_inputBusIsSidechain.push_back(sidechain);
            m_inputBusSidechainOffset.push_back(sidechain ? sidechainOffset : -1);
            if (sidechain)
                sidechainOffset += channels;
        }
        m_sidechainChannelCount = sidechainOffset;
        // One persistent channel-pointer slot per input bus; buildInputBuses()
        // points folded-mono buses at these so the array outlives process().
        m_monoFoldInPtrs.assign(static_cast<size_t>(std::max(1, nIn)), nullptr);
        m_outputBusChannels.clear();
        m_outputBusNames.clear();
        for (int32 i = 0; i < nOut; ++i) {
            BusInfo bi{};
            if (m_component->getBusInfo(kAudio, kOutput, i, bi) == kResultTrue) {
                m_outputBusChannels.push_back(bi.channelCount);
                m_outputBusNames.push_back(QString::fromUtf16(
                    reinterpret_cast<const char16_t*>(bi.name)));
            } else {
                m_outputBusChannels.push_back(2);
                m_outputBusNames.push_back(QString("Output %1").arg(i + 1));
            }
        }
    }

    m_filePath = path;
    return true;
}

bool VST3Instance::activateComponent(Steinberg::Vst::IComponent* component,
                                     Steinberg::Vst::IAudioProcessor* processor,
                                     Steinberg::Vst::ProcessSetup& setup) {
    if (!component || !processor) return false;

    if (processor->setupProcessing(setup) != kResultTrue) return false;

    // Re-apply bus activation now that setupProcessing() has built the host-bus
    // map: an activateBus() call before it is a no-op, which leaves every bus
    // "host inactive" and makes the plugin process silence (JUCE's
    // ClientBufferMapper clears host-inactive input buses and skips copying
    // their output back). See juce_VST3Common.h DynamicChannelMapping.
    activateComponentBuses(component);

    component->setActive(true);
    processor->setProcessing(true);
    return true;
}

void VST3Instance::refreshScratchPtrs() {
    m_expandInPtrs.clear();
    for (auto& b : m_expandInScratch) m_expandInPtrs.push_back(b.data());
    m_reduceOutPtrs.clear();
    for (auto& b : m_reduceOutScratch) m_reduceOutPtrs.push_back(b.data());
}

void VST3Instance::buildInputBuses(const InputBusLayout& layout,
                                   AudioBusBuffers* inBuses, int numInBuses,
                                   float** inputBuffers, int numSamples, int numChannels) {
    if (!inBuses) return;

    // Folded-mono buses need a channel-pointer array that survives this call;
    // fall back to a defensive resize if the caller under-sized the storage.
    if (layout.monoFoldPtrs &&
        layout.monoFoldPtrs->size() < static_cast<size_t>(numInBuses))
        layout.monoFoldPtrs->resize(static_cast<size_t>(numInBuses), nullptr);

    int expandBase = 0;
    for (int32 i = 0; i < numInBuses; ++i) {
        const InputBusInfo info = inputBusInfo(layout, i, numChannels);
        if (info.sidechain) {
            configureSidechainInputBus(inBuses[i], info.channels, info.offset,
                                       layout.sidechainChannelCount, layout.sidechainPtrs);
            continue;
        }
        configureMainInputBus(inBuses[i], info.channels, inputBuffers, numSamples,
                              numChannels, expandBase, i, layout);
        expandBase += info.channels;
    }
}

void VST3Instance::configureInputBuses(AudioBusBuffers* inBuses, int numInBuses,
                                       float** inputBuffers, int numSamples, int numChannels) {
    InputBusLayout layout;
    layout.busChannels = &m_inputBusChannels;
    layout.busIsSidechain = &m_inputBusIsSidechain;
    layout.busSidechainOffset = &m_inputBusSidechainOffset;
    layout.sidechainPtrs = &m_sidechainChannelPtrs;
    layout.sidechainChannelCount = m_sidechainChannelCount;
    layout.monoFoldPtrs = &m_monoFoldInPtrs;
    layout.expandPtrs = &m_expandInPtrs;
    layout.monoScratch = &m_monoScratch;
    buildInputBuses(layout, inBuses, numInBuses, inputBuffers, numSamples, numChannels);
}

bool VST3Instance::configureOutputBuses(AudioBusBuffers* outBuses, int numOutBuses,
                                        float** outputBuffers, int numChannels,
                                        int pluginOutChannels) {
    const bool reduceOut = pluginOutChannels > numChannels && outputBuffers && numChannels > 0 &&
                           pluginOutChannels <= static_cast<int32>(m_reduceOutPtrs.size());
    int32 channelOffset = 0;
    for (int32 i = 0; i < numOutBuses; ++i) {
        int32 busChannels = (i < static_cast<int32>(m_outputBusChannels.size()))
                                ? m_outputBusChannels[i] : numChannels;
        outBuses[i].silenceFlags = 0;
        if (reduceOut) {
            // More plugin output channels than the host: render into the
            // scratch and fold down after process().
            outBuses[i].numChannels = busChannels;
            outBuses[i].channelBuffers32 = m_reduceOutPtrs.data() + channelOffset;
        } else {
            int32 available = std::max<int32>(0, numChannels - channelOffset);
            outBuses[i].numChannels = std::min(busChannels, available);
            outBuses[i].channelBuffers32 = outputBuffers ? outputBuffers + channelOffset : nullptr;
        }
        channelOffset += busChannels;
    }
    return reduceOut;
}

bool VST3Instance::activate(double sampleRate, int maxBlockSize) {
    if (!m_component || !m_audioProcessor) return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_monoScratch.resize(static_cast<size_t>(maxBlockSize));
    {
        // One persistent channel-pointer slot per input bus (see load()).
        int32 nIn = m_component->getBusCount(kAudio, kInput);
        m_monoFoldInPtrs.assign(static_cast<size_t>(std::max(1, nIn)), nullptr);
    }
    m_sidechainBuffers.assign(static_cast<size_t>(m_sidechainChannelCount),
                              std::vector<float>(static_cast<size_t>(maxBlockSize), 0.0f));
    m_sidechainChannelPtrs.clear();
    m_sidechainChannelPtrs.reserve(m_sidechainBuffers.size());
    for (auto& buf : m_sidechainBuffers)
        m_sidechainChannelPtrs.push_back(buf.data());

    int totalIn = 0;
    for (size_t i = 0; i < m_inputBusChannels.size(); ++i) {
        const bool side = i < m_inputBusIsSidechain.size() && m_inputBusIsSidechain[i];
        if (!side) totalIn += m_inputBusChannels[i];
    }
    int totalOut = 0;
    for (auto cc : m_outputBusChannels) totalOut += cc;
    m_expandInScratch.assign(static_cast<size_t>(std::max(1, totalIn)),
                             std::vector<float>(static_cast<size_t>(maxBlockSize), 0.0f));
    m_reduceOutScratch.assign(static_cast<size_t>(std::max(1, totalOut)),
                              std::vector<float>(static_cast<size_t>(maxBlockSize), 0.0f));
    refreshScratchPtrs();

    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;

    if (!activateComponent(m_component.get(), m_audioProcessor.get(), setup))
        return false;

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

    // Never hand a plugin more samples than the setup called for: both our
    // adaptation scratch and the plugin's own buffers (e.g. DPF's dummy input)
    // are sized for maxSamplesPerBlock.
    numSamples = std::min(numSamples, m_maxBlockSize);

    int32 numInBuses = m_component ? m_component->getBusCount(kAudio, kInput) : 1;
    int32 numOutBuses = m_component ? m_component->getBusCount(kAudio, kOutput) : 1;
    if (numInBuses < 1) numInBuses = 1;
    if (numOutBuses < 1) numOutBuses = 1;

    // Keep the adaptation scratch large enough for this block.
    for (auto& b : m_expandInScratch)
        if (b.size() < static_cast<size_t>(numSamples)) b.resize(static_cast<size_t>(numSamples));
    for (auto& b : m_reduceOutScratch)
        if (b.size() < static_cast<size_t>(numSamples)) b.resize(static_cast<size_t>(numSamples));
    refreshScratchPtrs();

    std::vector<AudioBusBuffers> inBuses(numInBuses);
    configureInputBuses(inBuses.data(), numInBuses, inputBuffers, numSamples, numChannels);

    int32 pluginOutChannels = 0;
    for (auto cc : m_outputBusChannels) pluginOutChannels += cc;

    std::vector<AudioBusBuffers> outBuses(numOutBuses);
    const bool reduceOut = configureOutputBuses(outBuses.data(), numOutBuses, outputBuffers,
                                                numChannels, pluginOutChannels);

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

    // Fold the plugin's wider output down to the host's channel count (e.g. a
    // stereo effect on a mono track).
    if (reduceOut && result == kResultTrue)
        reduceChannelsToHost(m_reduceOutPtrs.data(), pluginOutChannels,
                             outputBuffers, numChannels, numSamples);

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

int VST3Instance::audioOutputChannels() const {
    int total = 0;
    for (auto cc : m_outputBusChannels)
        total += cc;
    return total;
}

std::vector<QString> VST3Instance::audioOutputNames() const {
    std::vector<QString> names;
    for (size_t b = 0; b < m_outputBusChannels.size(); ++b) {
        int cc = m_outputBusChannels[b];
        QString busName = (b < m_outputBusNames.size() && !m_outputBusNames[b].isEmpty())
                              ? m_outputBusNames[b]
                              : QString("Output %1").arg(b + 1);
        for (int j = 0; j < cc; ++j) {
            if (cc == 1)
                names.push_back(busName);
            else if (cc == 2)
                names.push_back(busName + (j == 0 ? " L" : " R"));
            else
                names.push_back(QString("%1 %2").arg(busName).arg(j + 1));
        }
    }
    return names;
}

int VST3Instance::latencySamples() const {
    if (m_audioProcessor) return m_audioProcessor->getLatencySamples();
    return 0;
}

float* VST3Instance::sidechainBuffer(int channel) {
    if (channel < 0 || channel >= static_cast<int>(m_sidechainBuffers.size()))
        return nullptr;
    return m_sidechainBuffers[static_cast<size_t>(channel)].data();
}

void VST3Instance::clearSidechainBuffers() {
    for (auto& buf : m_sidechainBuffers)
        std::fill(buf.begin(), buf.end(), 0.0f);
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
        bool ok = runSigGuarded([&] {
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
        });
        if (!ok)
            qWarning() << m_name << ": VST3 state save crashed, skipping state";
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
        bool ok = runSigGuarded([&] {
            m_component->setState(&stream);

            if (m_controller && m_separateController) {
                stream.reset();
                m_controller->setComponentState(&stream);
            }
        });
        if (!ok)
            qWarning() << m_name << ": VST3 state restore crashed, skipping state";
    }
}
