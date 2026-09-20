#pragma once
#include "SigGuard.h"
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
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

// Enumerates factory classes via getClassInfo inside a SIGSEGV guard. Some old
// plugins (DPF-based) crash inside getClassInfo, so the crash is caught and the
// caller can fall back to binary scanning. Returns false if enumeration crashed.
inline bool enumerateClassesGuarded(Steinberg::IPluginFactory* factory,
                                    std::vector<Steinberg::PClassInfo>& out) {
    using namespace Steinberg;
    bool ok = runSigGuarded([&] {
        int32 n = factory->countClasses();
        for (int32 i = 0; i < n; ++i) {
            PClassInfo ci{};
            if (factory->getClassInfo(i, &ci) == kResultOk)
                out.push_back(ci);
        }
    });
    return ok;
}

// UID discovery from class metadata. Never constructs a plugin instance:
// createInstance is reserved for the real component and is the only call that
// runs arbitrary plugin code. Matching on the "Audio Module Class" category is
// ABI-safe (the category is a plain char[] in PClassInfo shared by the SDK),
// unlike the older binary memory scan which called createInstance for every
// 4-byte window in the .so.
//
// The memory scan is kept only as a last-resort fallback for plugins whose
// getClassInfo crashes (see findComponentUIDByScan).
inline bool findComponentUIDByClassInfo(Steinberg::IPluginFactory* factory,
                                        Steinberg::TUID outUID) {
    using namespace Steinberg;
    std::memset(outUID, 0, 16);
    std::vector<PClassInfo> classes;
    if (!enumerateClassesGuarded(factory, classes)) return false;

    for (const auto& ci : classes) {
        if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
        std::memcpy(outUID, ci.cid, 16);
        return true;
    }
    return false;
}

// Fallback UID discovery for plugins whose getClassInfo cannot be used. Calls
// createInstance on successive 16-byte windows of the binary until one is
// accepted as an IComponent. This executes plugin code (the component
// constructor) and is expensive; for JUCE 8 plugins every createInstance also
// spins up a ScopedRunLoop/message thread, so this must never run before the
// class-info path.
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
            ((FUnknown*)obj)->release();
            return true;
        }
    }
    return false;
}

// Combined discovery: class metadata first (no plugin instantiation), then the
// crash-guarded binary scan for plugins like MT-PowerDrumKit whose getClassInfo
// is unusable.
inline bool findComponentUID(Steinberg::IPluginFactory* factory,
                             const std::string& soPath,
                             Steinberg::TUID outUID) {
    if (findComponentUIDByClassInfo(factory, outUID)) return true;
    return findComponentUIDByScan(factory, soPath, outUID);
}

// True when a pipe-separated VST3 subcategory list contains `token` exactly
// (so "Fx|Instrument" matches "Instrument" but "Fx|Instruments" would not).
inline bool subCategoriesContain(const char* subCategories, const char* token) {
    if (!subCategories || !token) return false;
    const size_t tokenLen = std::strlen(token);
    const char* p = subCategories;
    while (*p) {
        const char* end = std::strchr(p, '|');
        const size_t len = end ? static_cast<size_t>(end - p) : std::strlen(p);
        if (len == tokenLen && std::strncmp(p, token, len) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

// Detects whether the component class `uid` declares itself an instrument in
// its PClassInfo2 subcategories. Metadata only — the plugin is not
// instantiated. Returns false when the factory has no class-info v2 support;
// VST3Instance::load() still determines the real bus layout once loaded.
inline bool componentIsInstrument(Steinberg::IPluginFactory* factory,
                                  const Steinberg::TUID uid) {
    using namespace Steinberg;
    IPluginFactory2* factory2 = nullptr;
    if (factory->queryInterface(IPluginFactory2::iid, (void**)&factory2) != kResultOk ||
        factory2 == nullptr)
        return false;

    bool isInstrument = false;
    runSigGuarded([&] {
        int32 n = factory2->countClasses();
        for (int32 i = 0; i < n; ++i) {
            PClassInfo2 ci{};
            if (factory2->getClassInfo2(i, &ci) != kResultOk) continue;
            if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
            if (std::memcmp(ci.cid, uid, 16) != 0) continue;
            isInstrument = subCategoriesContain(ci.subCategories, PlugType::kInstrument);
            break;
        }
    });

    factory2->release();
    return isInstrument;
}

// Detects whether a VST3 bundle contains an instrument (a component whose
// PClassInfo2 subcategories include "Instrument") using metadata only. Owns
// its own dlopen handle so it is safe to call after the caller closed its own.
// Never constructs the plugin.
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
