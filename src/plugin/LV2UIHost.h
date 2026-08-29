#pragma once
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <functional>
#include <cstdint>
#include <map>
#include <string>

class LV2UIHost {
public:
    LV2UIHost();
    ~LV2UIHost();

    bool open(const char* pluginUri, const char* bundlePath, const char* binaryPath,
              LV2_URID_Map* uridMap, const LV2_Options_Option* options,
              LV2_Handle pluginHandle, unsigned long parentWindowId);
    void close();

    // Show/hide via the ui:showInterface extension (separate-window UIs).
    bool show();
    void hide();

    // Best-effort window-manager hints for a detached external UI window:
    // make it transient to `transientParentXid` and keep it above other
    // windows (matching the embedded PluginWindow stay-on-top behaviour).
    // Returns true only once the hints are verified as stored by the window
    // manager — keep calling until it returns true (the WM may ignore early
    // applications, so verification happens on a later call).
    bool applyWindowHints(unsigned long transientParentXid);

    // The first verified external toplevel window (0 while unknown / not yet
    // mapped by the UI process).
    unsigned long externalWindow() const;
    // Whether the external toplevel still exists (checked via the X window
    // tree — safe against destroyed foreign windows).
    bool externalWindowAlive() const;
    // Raise the external toplevel and re-assert the window hints.
    bool raiseExternalWindow();

    unsigned long getChildWindow() const;
    bool hasIdleInterface() const;
    bool idle();
    void sendPortEvent(int portIndex, float value);
    void sendAtomEvent(int portIndex, uint32_t bufferSize, uint32_t format, const void* buffer);

    bool getChildSize(int& width, int& height) const;

    void setPortMap(const std::map<std::string, uint32_t>& symbolToIndex) {
        m_symbolToIndex = symbolToIndex;
    }

    std::function<void(int, float)> portWriteCallback;
    std::function<void(int, uint32_t, uint32_t, uint32_t, const void*)> atomWriteCallback;

private:
    struct Impl;
    Impl* m_impl = nullptr;
    std::map<std::string, uint32_t> m_symbolToIndex;
};
