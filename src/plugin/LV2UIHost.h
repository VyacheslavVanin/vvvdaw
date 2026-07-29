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

    unsigned long getChildWindow() const;
    bool hasIdleInterface() const;
    void idle();
    void sendPortEvent(int portIndex, float value);

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
