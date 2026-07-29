#include "LV2UIHost.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <csignal>
#include <setjmp.h>

#ifdef DEBUG_LV2UI
#define LV2UI_LOG(fmt, ...) fprintf(stderr, "LV2UIHost: " fmt "\n", ##__VA_ARGS__)
#else
#define LV2UI_LOG(fmt, ...) ((void)0)
#endif

static bool s_xErrorTriggered = false;
static pthread_mutex_t s_xErrorMutex = PTHREAD_MUTEX_INITIALIZER;

static int temporaryXErrorHandler(Display*, XErrorEvent*) {
    s_xErrorTriggered = true;
    return 0;
}

struct LV2UIHost::Impl {
    Display* display = nullptr;
    Window parentWindow = 0;
    Window childWindow = 0;
    bool ownsParentWindow = false;

    void* dlHandle = nullptr;
    LV2UI_Descriptor* descriptor = nullptr;
    LV2UI_Handle uiHandle = nullptr;
    LV2UI_Widget widget = nullptr;

    LV2_URID_Map* uridMap = nullptr;
    LV2_Handle pluginHandle = nullptr;

    LV2UI_Port_Map portMapData = {};
    LV2_Feature portMapFeature = {};
    LV2UI_Resize resizeData = {};
    LV2_Feature resizeFeature = {};
    LV2_Feature touchFeature = {};
    LV2_Feature parentFeature = {};
    LV2_Feature optionsFeature = {};
    LV2_Feature uridMapFeature = {};
    const LV2_Feature* features[7] = {};

    LV2UIHost* owner = nullptr;

    static void uiWriteFunction(LV2UI_Controller controller, uint32_t portIndex,
                                uint32_t bufferSize, uint32_t format, const void* buffer);
    static uint32_t uiPortMap(LV2UI_Feature_Handle handle, const char* portSymbol);
    static int uiResize(LV2UI_Feature_Handle handle, int width, int height);
    static void uiTouch(LV2UI_Feature_Handle handle, uint32_t portIndex, bool grabbed);
};

uint32_t LV2UIHost::Impl::uiPortMap(LV2UI_Feature_Handle handle, const char* portSymbol) {
    if (!handle || !portSymbol) return LV2UI_INVALID_PORT_INDEX;
    auto* impl = static_cast<Impl*>(handle);
    if (!impl->owner) return LV2UI_INVALID_PORT_INDEX;
    auto it = impl->owner->m_symbolToIndex.find(portSymbol);
    if (it != impl->owner->m_symbolToIndex.end())
        return it->second;
    return LV2UI_INVALID_PORT_INDEX;
}

int LV2UIHost::Impl::uiResize(LV2UI_Feature_Handle handle, int width, int height) {
    auto* impl = static_cast<Impl*>(handle);
    if (!impl || !impl->display || !impl->parentWindow) return -1;
    XResizeWindow(impl->display, impl->parentWindow, width, height);
    XSync(impl->display, False);
    return 0;
}

void LV2UIHost::Impl::uiTouch(LV2UI_Feature_Handle, uint32_t, bool) {}

void LV2UIHost::Impl::uiWriteFunction(LV2UI_Controller controller, uint32_t portIndex,
                                       uint32_t bufferSize, uint32_t format, const void* buffer) {
    auto* impl = static_cast<Impl*>(controller);
    if (format == 0) {
        if (buffer && bufferSize >= sizeof(float) && impl->owner->portWriteCallback) {
            float value = *static_cast<const float*>(buffer);
            impl->owner->portWriteCallback(static_cast<int>(portIndex), value);
        }
    } else {
        if (impl->owner->atomWriteCallback) {
            impl->owner->atomWriteCallback(static_cast<int>(portIndex), bufferSize,
                                           format, format, buffer);
        }
    }
}

LV2UIHost::LV2UIHost() : m_impl(new Impl) {
    m_impl->owner = this;
}

LV2UIHost::~LV2UIHost() {
    close();
    delete m_impl;
}

// Scan DPF's UI object and reachable sub-objects for any window IDs used as
// function pointers. After instantiate, DPF stores the parent window ID in
// fParent (and possibly other internal fields) and later uses it as a
// rendering callback. Zeroing these values prevents the crash since DPF
// null-checks before calling.
static void patchWindowIds(void* uiHandle, uintptr_t needle) {
    if (needle == 0 || uiHandle == nullptr) return;
    uintptr_t* obj = static_cast<uintptr_t*>(uiHandle);
    for (size_t i = 4; i < 128; ++i) {
        if (obj[i] == needle) {
            LV2UI_LOG("patch win %lu at uiHandle[%zu]", (unsigned long)needle, i);
            obj[i] = 0;
        }
    }
    static const uintptr_t kMinHeap = 0x500000000000ULL;
    static const uintptr_t kMaxHeap = 0x800000000000ULL;
    for (size_t off : {1, 2, 3, 4, 5, 6, 7, 8}) {
        uintptr_t uptr = obj[off];
        if (uptr < kMinHeap || uptr > kMaxHeap)
            continue;
        uintptr_t* sub = reinterpret_cast<uintptr_t*>(uptr);
        for (size_t j = 0; j < 128; ++j) {
            if (sub[j] == needle) {
                LV2UI_LOG("patch win %lu at uiHandle[%zu]->[%zu]",
                          (unsigned long)needle, off, j);
                sub[j] = 0;
            }
        }
    }
}

static void patchAfterCrash(void* uiHandle, uintptr_t parentWin, uintptr_t childWin) {
    patchWindowIds(uiHandle, parentWin);
    if (childWin && childWin != parentWin)
        patchWindowIds(uiHandle, childWin);
}

bool LV2UIHost::open(const char* pluginUri, const char* bundlePath, const char* binaryPath,
                     LV2_URID_Map* uridMap, const LV2_Options_Option* options,
                     LV2_Handle pluginHandle, unsigned long parentWindowId) {
    if (!pluginUri || !bundlePath || !binaryPath || !uridMap || !pluginHandle)
        return false;

    XInitThreads();

    m_impl->uridMap = uridMap;
    m_impl->pluginHandle = pluginHandle;

    m_impl->dlHandle = dlopen(binaryPath, RTLD_NOW);
    if (!m_impl->dlHandle) {
        LV2UI_LOG("dlopen failed: %s", dlerror());
        return false;
    }

    using EntryFunc = LV2UI_Descriptor* (*)(uint32_t);
    auto entry = reinterpret_cast<EntryFunc>(dlsym(m_impl->dlHandle, "lv2ui_descriptor"));
    if (!entry) {
        LV2UI_LOG("dlsym(lv2ui_descriptor) failed: %s", dlerror());
        dlclose(m_impl->dlHandle);
        m_impl->dlHandle = nullptr;
        return false;
    }

    m_impl->descriptor = entry(0);
    if (!m_impl->descriptor || !m_impl->descriptor->instantiate) {
        LV2UI_LOG("invalid descriptor or no instantiate");
        dlclose(m_impl->dlHandle);
        m_impl->dlHandle = nullptr;
        return false;
    }

    // Always open our own Display — needed for XGetWindowAttributes on the child window
    m_impl->display = XOpenDisplay(nullptr);
    if (!m_impl->display) {
        LV2UI_LOG("XOpenDisplay failed");
        dlclose(m_impl->dlHandle);
        m_impl->dlHandle = nullptr;
        return false;
    }

    if (parentWindowId != 0) {
        // Embed in the caller's window
        m_impl->parentWindow = parentWindowId;
        m_impl->ownsParentWindow = false;
    } else {
        // Create an intermediate parent window with WM_STATE so the window
        // manager treats it as a managed toplevel. pugl creates its child as
        // a subwindow of this parent, avoiding the cross-Display crash (no
        // Qt window involved) while still getting proper window decorations
        // and event delivery.
        Window root = RootWindow(m_impl->display, DefaultScreen(m_impl->display));
        m_impl->parentWindow = XCreateSimpleWindow(
            m_impl->display, root, 0, 0, 800, 500, 0,
            BlackPixel(m_impl->display, DefaultScreen(m_impl->display)),
            WhitePixel(m_impl->display, DefaultScreen(m_impl->display)));

        // Set window properties for proper WM management
        XStoreName(m_impl->display, m_impl->parentWindow, "LV2 Native UI");
        XClassHint classHint{};
        classHint.res_name = const_cast<char*>("lv2_native_ui");
        classHint.res_class = const_cast<char*>("VVVDaw");
        XSetClassHint(m_impl->display, m_impl->parentWindow, &classHint);

        // WM_DELETE_WINDOW protocol
        Atom wmDelete = XInternAtom(m_impl->display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(m_impl->display, m_impl->parentWindow, &wmDelete, 1);

        // Select events on the parent so we can see child creation and mapping
        XSelectInput(m_impl->display, m_impl->parentWindow, SubstructureNotifyMask);

        m_impl->ownsParentWindow = true;
    }

    m_impl->portMapData.handle = m_impl;
    m_impl->portMapData.port_index = Impl::uiPortMap;

    m_impl->portMapFeature.URI = LV2_UI__portMap;
    m_impl->portMapFeature.data = &m_impl->portMapData;

    m_impl->resizeData = {};
    m_impl->resizeData.handle = m_impl;
    m_impl->resizeData.ui_resize = Impl::uiResize;

    m_impl->resizeFeature.URI = LV2_UI__resize;
    m_impl->resizeFeature.data = &m_impl->resizeData;

    m_impl->touchFeature.URI = LV2_UI__touch;
    m_impl->touchFeature.data = m_impl;

    m_impl->parentFeature.URI = LV2_UI__parent;
    m_impl->parentFeature.data = (void*)(uintptr_t)m_impl->parentWindow;

    m_impl->optionsFeature.URI = LV2_OPTIONS__options;
    m_impl->optionsFeature.data = const_cast<LV2_Options_Option*>(options);

    m_impl->uridMapFeature.URI = LV2_URID__map;
    m_impl->uridMapFeature.data = uridMap;

    int idx = 0;
    m_impl->features[idx++] = &m_impl->parentFeature;
    m_impl->features[idx++] = &m_impl->portMapFeature;
    m_impl->features[idx++] = &m_impl->resizeFeature;
    m_impl->features[idx++] = &m_impl->touchFeature;
    m_impl->features[idx++] = &m_impl->optionsFeature;
    m_impl->features[idx++] = &m_impl->uridMapFeature;
    m_impl->features[idx] = nullptr;

    m_impl->widget = nullptr;
    m_impl->uiHandle = m_impl->descriptor->instantiate(
        m_impl->descriptor,
        pluginUri,
        bundlePath,
        Impl::uiWriteFunction,
        m_impl,
        &m_impl->widget,
        m_impl->features);

    if (!m_impl->uiHandle) {
        LV2UI_LOG("instantiate failed");
        XCloseDisplay(m_impl->display);
        m_impl->display = nullptr;
        dlclose(m_impl->dlHandle);
        m_impl->dlHandle = nullptr;
        return false;
    }

    if (m_impl->widget) {
        LV2UI_LOG("widget: %p uiHandle: %p", m_impl->widget, m_impl->uiHandle);

        // Patch DPF's internal window-ID function-pointer fields before any
        // rendering happens. This prevents the first knob interaction from
        // crashing. If any instances are missed, the sigsetjmp guard in idle()
        // catches them and re-patches.
        if (m_impl->parentWindow)
            patchWindowIds(m_impl->uiHandle, m_impl->parentWindow);

        // widget might be the UI object pointer (parent=0) or a Window ID (parent!=0)
        // Try using it as a Window ID first
        Window potentialWin = (Window)(uintptr_t)m_impl->widget;
        XWindowAttributes attrs{};
        if (XGetWindowAttributes(m_impl->display, potentialWin, &attrs)) {
            m_impl->childWindow = potentialWin;
            LV2UI_LOG("widget is a valid window %lu (size %dx%d)",
                      (unsigned long)potentialWin, attrs.width, attrs.height);
        } else {
            // widget is NOT a window — probably the UI object pointer.
            // Try to find the actual window by iterating root children.
            LV2UI_LOG("widget %p is NOT a window; searching root children", m_impl->widget);
            Window root = RootWindow(m_impl->display, DefaultScreen(m_impl->display));
            Window parent, *children = nullptr;
            unsigned int nchildren = 0;
            if (XQueryTree(m_impl->display, root, &root, &parent, &children, &nchildren)) {
                LV2UI_LOG("root has %u children", nchildren);
                for (unsigned int i = 0; i < nchildren; ++i) {
                    XWindowAttributes ca{};
                    if (XGetWindowAttributes(m_impl->display, children[i], &ca)) {
                        LV2UI_LOG("  child[%u] = %lu (map_state=%d, %dx%d)",
                                  i, (unsigned long)children[i], ca.map_state,
                                  ca.width, ca.height);
                    } else {
                        LV2UI_LOG("  child[%u] = %lu (cannot query)", i, (unsigned long)children[i]);
                    }
                }
                XFree(children);
            }
        }
    } else {
        pthread_mutex_lock(&s_xErrorMutex);
        XErrorHandler oldHandler = XSetErrorHandler(temporaryXErrorHandler);
        s_xErrorTriggered = false;

        Window root, parent;
        Window* children = nullptr;
        unsigned int nchildren = 0;
        if (m_impl->parentWindow)
            XQueryTree(m_impl->display, m_impl->parentWindow, &root, &parent, &children, &nchildren);

        XSetErrorHandler(oldHandler);
        pthread_mutex_unlock(&s_xErrorMutex);

        if (!s_xErrorTriggered && nchildren > 0 && children) {
            m_impl->childWindow = children[0];
            XFree(children);
        }
    }

    // Map our intermediate parent window (if we own it). The window manager
    // manages this toplevel; the pugl child window appears inside it.
    if (m_impl->ownsParentWindow && m_impl->parentWindow && m_impl->display) {
        XMapWindow(m_impl->display, m_impl->parentWindow);
        XSync(m_impl->display, False);
    }

    // Map the child and send a synthetic Expose to kickstart pugl rendering
    if (m_impl->childWindow && m_impl->display) {
        XMapWindow(m_impl->display, m_impl->childWindow);
        XSync(m_impl->display, False);

        XEvent ev{};
        ev.xexpose.type = Expose;
        ev.xexpose.window = m_impl->childWindow;
        ev.xexpose.x = 0;
        ev.xexpose.y = 0;
        ev.xexpose.width = 800;
        ev.xexpose.height = 500;
        ev.xexpose.count = 0;
        XSendEvent(m_impl->display, m_impl->childWindow, False, ExposureMask, &ev);
        XSync(m_impl->display, False);
    }

    LV2UI_LOG("UI instantiated, parent: %lu child: %lu",
              (unsigned long)m_impl->parentWindow, (unsigned long)m_impl->childWindow);
    return true;
}

static sigjmp_buf s_idleJmpBuf;
static bool s_idleCrashFlag = false;

static void idleCrashHandler(int) {
    s_idleCrashFlag = true;
    siglongjmp(s_idleJmpBuf, 1);
}

void LV2UIHost::close() {
    if (!m_impl) return;

    if (m_impl->descriptor && m_impl->uiHandle) {
        if (m_impl->descriptor->cleanup)
            m_impl->descriptor->cleanup(m_impl->uiHandle);
        m_impl->uiHandle = nullptr;
    }

    if (m_impl->ownsParentWindow && m_impl->parentWindow && m_impl->display) {
        XDestroyWindow(m_impl->display, m_impl->parentWindow);
    }
    m_impl->parentWindow = 0;
    m_impl->childWindow = 0;
    m_impl->widget = nullptr;

    if (m_impl->display) {
        XCloseDisplay(m_impl->display);
        m_impl->display = nullptr;
    }

    if (m_impl->dlHandle) {
        dlclose(m_impl->dlHandle);
        m_impl->dlHandle = nullptr;
    }

    m_impl->descriptor = nullptr;
}

unsigned long LV2UIHost::getChildWindow() const {
    return m_impl ? m_impl->childWindow : 0;
}

bool LV2UIHost::hasIdleInterface() const {
    if (!m_impl || !m_impl->descriptor || !m_impl->descriptor->extension_data) return false;
    return m_impl->descriptor->extension_data(LV2_UI__idleInterface) != nullptr;
}

void LV2UIHost::sendPortEvent(int portIndex, float value) {
    if (!m_impl || !m_impl->descriptor || !m_impl->uiHandle) return;
    if (m_impl->descriptor->port_event)
        m_impl->descriptor->port_event(m_impl->uiHandle, portIndex, sizeof(float), 0, &value);
}


void LV2UIHost::idle() {
    if (!m_impl || !m_impl->descriptor || !m_impl->descriptor->extension_data || !m_impl->uiHandle)
        return;

    struct sigaction oldSa, sa{};
    sa.sa_handler = idleCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &oldSa);

    s_idleCrashFlag = false;
    if (sigsetjmp(s_idleJmpBuf, 1) == 0) {
        auto* idle = reinterpret_cast<const LV2UI_Idle_Interface*>(
            m_impl->descriptor->extension_data(LV2_UI__idleInterface));
        if (idle && idle->idle) {
            int ret = idle->idle(m_impl->uiHandle);
            if (ret != 0)
                LV2UI_LOG("idle() returned %d (error)", ret);
        } else {
            LV2UI_LOG("idle interface has no idle() function");
        }
    }

    sigaction(SIGSEGV, &oldSa, nullptr);

    if (s_idleCrashFlag) {
        LV2UI_LOG("DPF bad function pointer caught — patching window IDs");
        patchAfterCrash(m_impl->uiHandle,
                        m_impl->parentWindow,
                        m_impl->childWindow);
    }
}

bool LV2UIHost::getChildSize(int& width, int& height) const {
    if (!m_impl || !m_impl->display || !m_impl->childWindow) return false;
    XWindowAttributes attrs{};
    if (XGetWindowAttributes(m_impl->display, m_impl->childWindow, &attrs)) {
        width = attrs.width;
        height = attrs.height;
        return true;
    }
    return false;
}
