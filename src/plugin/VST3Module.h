#pragma once
#include <public.sdk/source/vst/hosting/module.h>
#include <dlfcn.h>

namespace VST3 {
namespace Hosting {

inline Module::Ptr Module::create(const std::string& path, std::string& errorDescription) {
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        errorDescription = dlerror();
        return nullptr;
    }

    using GetFactoryFunc = Steinberg::IPluginFactory* (*)();
    auto getFactory = reinterpret_cast<GetFactoryFunc>(dlsym(handle, "GetPluginFactory"));
    if (!getFactory) {
        errorDescription = "GetPluginFactory not found";
        dlclose(handle);
        return nullptr;
    }

    Steinberg::IPluginFactory* rawFactory = getFactory();
    if (!rawFactory) {
        errorDescription = "GetPluginFactory returned null";
        dlclose(handle);
        return nullptr;
    }

    auto factory = Steinberg::owned(rawFactory);

    // Find first audio effect class
    bool found = false;
    for (uint32_t i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo ci;
        factory->getClassInfo(i, &ci);
        if (std::string(ci.category) == Steinberg::Vst::kVstAudioEffectClass) {
            found = true;
            break;
        }
    }
    if (!found) {
        errorDescription = "No audio effect class found";
        return nullptr;
    }

    class ModuleImpl : public Module {
    public:
        ModuleImpl(const std::string& p, PluginFactory&& f, void* h)
            : Module() {
            path = p;
            name = p;
            factory = std::move(f);
            handle = h;
        }
        ~ModuleImpl() override {
            if (handle) dlclose(handle);
        }
        bool load(const std::string&, std::string&) override { return true; }
        void* handle = nullptr;
    };

    auto mod = Steinberg::owned(new ModuleImpl(path, PluginFactory(factory), handle));
    return mod;
}

inline Module::PathList Module::getModulePaths() {
    PathList paths;
    const char* home = getenv("HOME");
    if (home) {
        std::string p = std::string(home) + "/.vst3";
        paths.push_back(p);
    }
    paths.push_back("/usr/lib/vst3");
    paths.push_back("/usr/local/lib/vst3");
    return paths;
}

inline Module::SnapshotList Module::getSnapshots(const std::string&) {
    return {};
}

inline Optional<std::string> Module::getModuleInfoPath(const std::string& modulePath) {
    return {};
}

} // namespace Hosting
} // namespace VST3
