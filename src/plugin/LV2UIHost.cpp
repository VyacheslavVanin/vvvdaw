#include "LV2UIHost.h"
#include "SigGuard.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <lv2/instance-access/instance-access.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <csignal>
#include <setjmp.h>
#include <set>

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

// Snapshot of the current toplevel (root children) window IDs.
static void collectRootChildren(Display* dpy, std::set<unsigned long>& out) {
    Window root, parent, *children = nullptr;
    unsigned int nchildren = 0;
    if (!XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                    &root, &parent, &children, &nchildren))
        return;
    for (unsigned int i = 0; i < nchildren; ++i)
        out.insert(static_cast<unsigned long>(children[i]));
    if (children) XFree(children);
}

// Request "always on top" for a toplevel window via the EWMH client message.
static void sendNetWmStateAbove(Display* dpy, Window w) {
    Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = netWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
    ev.xclient.data.l[1] = static_cast<long>(above);
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = 1; // source indication: normal application
    ev.xclient.data.l[4] = 0;
    XSendEvent(dpy, RootWindow(dpy, DefaultScreen(dpy)), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

struct LV2UIHost::Impl {
    Display* display = nullptr;
    Window parentWindow = 0;
    Window childWindow = 0;
    bool ownsParentWindow = false;
    // Toplevel windows that existed before the UI was opened; used to
    // identify the detached external UI window (root-children diff).
    std::set<unsigned long> rootWindowsBefore;
    // Detached external UI windows: candidates found and ones whose hints the
    // window manager has verified as stored.
    std::set<unsigned long> externalWindows;
    std::set<unsigned long> externalWindowsVerified;
    unsigned long transientParent = 0;
    // True when the UI runs detached (own toplevel in its own process, shown
    // via ui:showInterface) rather than embedded as an X11 child.
    bool detached = false;

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
    LV2UI_Touch touchData = {};
    LV2_Feature touchFeature = {};
    LV2_Feature parentFeature = {};
    LV2_Feature optionsFeature = {};
    LV2_Feature uridMapFeature = {};
    LV2_Feature instanceAccessFeature = {};
    const LV2_Feature* features[8] = {};

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

void LV2UIHost::Impl::uiTouch(LV2UI_Feature_Handle, uint32_t, bool) {
}

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
        // Detached mode (DPF ExternalWindow / ui:showInterface UIs, e.g.
        // ZynAddSubFX): the UI runs in its own process and manages its own
        // toplevel window, so the host creates no container window at all.
        // Snapshot the existing toplevels so the plugin window can later be
        // identified as a new root child and given window-manager hints.
        m_impl->parentWindow = 0;
        m_impl->ownsParentWindow = false;
        m_impl->detached = true;
        m_impl->rootWindowsBefore.clear();
        collectRootChildren(m_impl->display, m_impl->rootWindowsBefore);
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

    m_impl->touchData.handle = m_impl;
    m_impl->touchData.touch = Impl::uiTouch;

    m_impl->touchFeature.URI = LV2_UI__touch;
    m_impl->touchFeature.data = &m_impl->touchData;

    m_impl->parentFeature.URI = LV2_UI__parent;
    m_impl->parentFeature.data = (void*)(uintptr_t)m_impl->parentWindow;

    m_impl->optionsFeature.URI = LV2_OPTIONS__options;
    m_impl->optionsFeature.data = const_cast<LV2_Options_Option*>(options);

    m_impl->uridMapFeature.URI = LV2_URID__map;
    m_impl->uridMapFeature.data = uridMap;

    m_impl->instanceAccessFeature.URI = LV2_INSTANCE_ACCESS_URI;
    m_impl->instanceAccessFeature.data = pluginHandle;

    int idx = 0;
    m_impl->features[idx++] = &m_impl->parentFeature;
    m_impl->features[idx++] = &m_impl->portMapFeature;
    m_impl->features[idx++] = &m_impl->resizeFeature;
    m_impl->features[idx++] = &m_impl->touchFeature;
    m_impl->features[idx++] = &m_impl->optionsFeature;
    m_impl->features[idx++] = &m_impl->uridMapFeature;
    m_impl->features[idx++] = &m_impl->instanceAccessFeature;
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

    // Detached external UIs own their window inside a separate process; the
    // widget is not an embeddable X11 window of ours and gets no patching.
    if (parentWindowId != 0 && m_impl->widget) {
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

void LV2UIHost::close() {
    if (!m_impl) return;

    if (m_impl->descriptor && m_impl->uiHandle) {
        if (m_impl->detached)
            hide();
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
    m_impl->detached = false;
    m_impl->transientParent = 0;
    m_impl->rootWindowsBefore.clear();
    m_impl->externalWindows.clear();
    m_impl->externalWindowsVerified.clear();

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

bool LV2UIHost::show() {
    if (!m_impl || !m_impl->descriptor || !m_impl->descriptor->extension_data || !m_impl->uiHandle)
        return false;
    auto* showIface = reinterpret_cast<const LV2UI_Show_Interface*>(
        m_impl->descriptor->extension_data(LV2_UI__showInterface));
    if (!showIface || !showIface->show) return false;
    bool ok = runSigGuarded([&] { showIface->show(m_impl->uiHandle); });
    if (!ok) return false;
    LV2UI_LOG("show() done");
    return true;
}

void LV2UIHost::hide() {
    if (!m_impl || !m_impl->descriptor || !m_impl->descriptor->extension_data || !m_impl->uiHandle)
        return;
    auto* showIface = reinterpret_cast<const LV2UI_Show_Interface*>(
        m_impl->descriptor->extension_data(LV2_UI__showInterface));
    if (!showIface || !showIface->hide) return;
    runSigGuarded([&] { showIface->hide(m_impl->uiHandle); });
}

namespace {

// Whether the window's _NET_WM_STATE property currently contains ABOVE.
bool readNetWmStateAbove(Display* dpy, Window w) {
    Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", True);
    if (!netWmState) return false;
    Atom type = 0;
    int fmt = 0;
    unsigned long nitems = 0, bytes = 0;
    unsigned char* prop = nullptr;
    bool hasAbove = false;
    if (XGetWindowProperty(dpy, w, netWmState, 0, 64, False, XA_ATOM,
                           &type, &fmt, &nitems, &bytes, &prop) == Success &&
        prop && type == XA_ATOM) {
        Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", True);
        auto* atoms = reinterpret_cast<Atom*>(prop);
        for (unsigned long i = 0; i < nitems; ++i)
            if (above && atoms[i] == above) hasAbove = true;
    }
    if (prop) XFree(prop);
    return hasAbove;
}

bool windowIsViewableRootChild(Display* dpy, Window w) {
    Window root, parent, *children = nullptr;
    unsigned int nchildren = 0;
    bool found = false;
    if (XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                   &root, &parent, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; ++i)
            if (children[i] == w) found = true;
        if (children) XFree(children);
    }
    return found;
}

} // namespace

bool LV2UIHost::applyWindowHints(unsigned long transientParentXid) {
    if (!m_impl || !m_impl->display) return false;
    Display* dpy = m_impl->display;
    m_impl->transientParent = transientParentXid;
    Window root, parent, *children = nullptr;
    unsigned int nchildren = 0;
    if (!XQueryTree(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                    &root, &parent, &children, &nchildren))
        return false;

    auto& candidates = m_impl->externalWindows;
    auto& verified = m_impl->externalWindowsVerified;
    for (unsigned int i = 0; i < nchildren; ++i) {
        Window w = children[i];
        // Only windows that appeared after the UI was opened can be the
        // external UI's toplevel.
        if (m_impl->rootWindowsBefore.count(static_cast<unsigned long>(w)))
            continue;
        XWindowAttributes attrs{};
        if (!XGetWindowAttributes(dpy, w, &attrs) || attrs.map_state != IsViewable)
            continue;
        candidates.insert(static_cast<unsigned long>(w));

        // The WM may ignore state changes on windows it has not taken over
        // yet (mutter clears them when it starts managing), so keep applying
        // until the property verifies — on a later call.
        bool above = readNetWmStateAbove(dpy, w);
        if (above) {
            if (transientParentXid)
                XSetTransientForHint(dpy, w, static_cast<Window>(transientParentXid));
            verified.insert(static_cast<unsigned long>(w));
            LV2UI_LOG("verified hints on toplevel %lu", static_cast<unsigned long>(w));
            continue;
        }
        sendNetWmStateAbove(dpy, w);
        LV2UI_LOG("requested ABOVE on toplevel %lu", static_cast<unsigned long>(w));
    }
    if (children) XFree(children);

    if (candidates.empty()) return false;
    return verified.size() == candidates.size();
}

unsigned long LV2UIHost::externalWindow() const {
    if (!m_impl || m_impl->externalWindowsVerified.empty()) return 0;
    return *m_impl->externalWindowsVerified.begin();
}

bool LV2UIHost::externalWindowAlive() const {
    if (!m_impl || !m_impl->display || m_impl->externalWindowsVerified.empty())
        return false;
    Display* dpy = m_impl->display;
    for (unsigned long w : m_impl->externalWindowsVerified)
        if (windowIsViewableRootChild(dpy, static_cast<Window>(w)))
            return true;
    return false;
}

bool LV2UIHost::raiseExternalWindow() {
    if (!externalWindowAlive()) return false;
    Display* dpy = m_impl->display;
    for (unsigned long w : m_impl->externalWindowsVerified) {
        if (!windowIsViewableRootChild(dpy, static_cast<Window>(w)))
            continue;
        Window win = static_cast<Window>(w);
        if (m_impl->transientParent)
            XSetTransientForHint(dpy, win, static_cast<Window>(m_impl->transientParent));
        sendNetWmStateAbove(dpy, win);
        XRaiseWindow(dpy, win);
        LV2UI_LOG("raised external toplevel %lu", w);
    }
    XFlush(dpy);
    return true;
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

void LV2UIHost::sendAtomEvent(int portIndex, uint32_t bufferSize, uint32_t format, const void* buffer) {
    if (!m_impl || !m_impl->descriptor || !m_impl->uiHandle) return;
    if (m_impl->descriptor->port_event)
        m_impl->descriptor->port_event(m_impl->uiHandle, portIndex, bufferSize, format, buffer);
}


bool LV2UIHost::idle() {
    if (!m_impl || !m_impl->descriptor || !m_impl->descriptor->extension_data || !m_impl->uiHandle)
        return false;

    bool idleOk = true;
    bool ok = runSigGuarded([&] {
        auto* idle = reinterpret_cast<const LV2UI_Idle_Interface*>(
            m_impl->descriptor->extension_data(LV2_UI__idleInterface));
        if (idle && idle->idle) {
            int ret = idle->idle(m_impl->uiHandle);
            if (ret != 0) {
                LV2UI_LOG("idle() returned %d (error)", ret);
                idleOk = false;
            }
        } else {
            LV2UI_LOG("idle interface has no idle() function");
        }
    });

    if (!ok) {
        LV2UI_LOG("DPF bad function pointer caught — patching window IDs");
        patchAfterCrash(m_impl->uiHandle,
                        m_impl->parentWindow,
                        m_impl->childWindow);
    }
    return ok && idleOk;
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
