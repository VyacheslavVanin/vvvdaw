#pragma once
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <csignal>
#include <csetjmp>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>

namespace VST3Scan {

using namespace Steinberg;
using namespace Steinberg::Vst;

// ABI-independent UID discovery: scans the .so for a 16-byte chunk that
// createInstance accepts as an IComponent. Used instead of getClassInfo*
// because the installed DPF-based plugins were built against older VST3 SDKs
// whose PClassInfo layout mismatches the current headers (getClassInfo crashes).
inline bool findComponentUIDByScan(Steinberg::IPluginFactory* factory,
                                   const std::string& soPath,
                                   Steinberg::TUID outUID) {
    using namespace Steinberg;
    std::memset(outUID, 0, 16);
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
        std::memcpy(tuid, buf.data() + i, 16);

        void* obj = nullptr;
        factory->createInstance(tuid, compIID, &obj);
        if (obj) {
            std::memcpy(outUID, tuid, 16);
            IPluginBase* base = nullptr;
            ((FUnknown*)obj)->queryInterface(IPluginBase::iid, (void**)&base);
            if (base) base->release();
            return true;
        }
    }
    return false;
}

namespace detail {
inline sigjmp_buf gClassInfoJmpBuf;
inline volatile sig_atomic_t gClassInfoCrashed = 0;
} // namespace detail

inline void classInfoCrashHandler(int) {
    detail::gClassInfoCrashed = 1;
    siglongjmp(detail::gClassInfoJmpBuf, 1);
}

// Enumerates factory classes via getClassInfo inside a SIGSEGV guard. Some old
// plugins (DPF-based) crash inside getClassInfo, so the crash is caught and the
// caller falls back to binary scanning. Returns false if enumeration crashed.
inline bool enumerateClassesGuarded(Steinberg::IPluginFactory* factory,
                                    std::vector<Steinberg::PClassInfo>& out) {
    using namespace Steinberg;
    struct sigaction oldSa, sa{};
    sa.sa_handler = classInfoCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &oldSa);

    detail::gClassInfoCrashed = 0;
    if (sigsetjmp(detail::gClassInfoJmpBuf, 1) == 0) {
        int32 n = factory->countClasses();
        for (int32 i = 0; i < n; ++i) {
            PClassInfo ci{};
            if (factory->getClassInfo(i, &ci) == kResultOk)
                out.push_back(ci);
        }
    }

    sigaction(SIGSEGV, &oldSa, nullptr);
    return detail::gClassInfoCrashed == 0;
}

// Fallback UID discovery for plugins whose UID is not present as contiguous
// bytes in the binary (e.g. MT-PowerDrumKit stores its GUID as instruction
// immediates). Relies on getClassInfo, crash-guarded for DPF-based plugins.
inline bool findComponentUIDByClassInfo(Steinberg::IPluginFactory* factory,
                                        Steinberg::TUID outUID) {
    using namespace Steinberg;
    std::memset(outUID, 0, 16);
    std::vector<PClassInfo> classes;
    if (!enumerateClassesGuarded(factory, classes)) return false;

    for (const auto& ci : classes) {
        void* obj = nullptr;
        factory->createInstance(ci.cid, IComponent::iid, (void**)&obj);
        if (obj) {
            std::memcpy(outUID, ci.cid, 16);
            IPluginBase* base = nullptr;
            ((FUnknown*)obj)->queryInterface(IPluginBase::iid, (void**)&base);
            if (base) base->release();
            return true;
        }
    }
    return false;
}

// Combined discovery: binary scan first (ABI-safe, covers DPF/lsp plugins),
// then crash-guarded getClassInfo for plugins like MT-PowerDrumKit.
inline bool findComponentUID(Steinberg::IPluginFactory* factory,
                             const std::string& soPath,
                             Steinberg::TUID outUID) {
    if (findComponentUIDByScan(factory, soPath, outUID)) return true;
    return findComponentUIDByClassInfo(factory, outUID);
}

// Detects whether a VST3 component (identified by UID) is an instrument: it
// must expose at least one event/MIDI input bus.
inline bool componentIsInstrument(Steinberg::IPluginFactory* factory,
                                  const Steinberg::TUID uid) {
    using namespace Steinberg;
    IComponent* comp = nullptr;
    factory->createInstance(uid, IComponent::iid, (void**)&comp);
    if (!comp) return false;

    Steinberg::Vst::HostApplication hostApp;
    comp->initialize(&hostApp);
    bool isInstrument = comp->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput) > 0;
    comp->terminate();
    comp->release();
    return isInstrument;
}

// Detects whether a VST3 bundle contains an instrument (a component with a
// MIDI/event input bus) using the combined UID discovery. Owns its own
// dlopen handle so it is safe to call after the caller closed its own.
inline bool bundleHasEventInput(const std::string& soPath) {
    using namespace Steinberg;
    void* handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) return false;

    using GetFactoryFunc = Steinberg::IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(handle, "GetPluginFactory"));
    IPluginFactory* factory = getFactory ? getFactory() : nullptr;
    if (!factory) {
        dlclose(handle);
        return false;
    }

    bool isInstrument = false;
    TUID uid = {0};
    if (findComponentUID(factory, soPath, uid))
        isInstrument = componentIsInstrument(factory, uid);

    dlclose(handle);
    return isInstrument;
}

} // namespace VST3Scan
