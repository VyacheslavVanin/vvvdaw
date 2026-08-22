#include "MidiInputManager.h"
#include <mutex>

#if defined(HAVE_RTMIDI)
#include <RtMidi.h>
#endif

struct MidiInputManager::Impl {
    std::mutex controlsMutex;
    MidiTransportControls controls;

#if defined(HAVE_RTMIDI)
    struct DeviceState {
        std::unique_ptr<RtMidiIn> in;
        MidiRingBuffer<MidiMessage> notes{4096};
        MidiRingBuffer<uint8_t> transport{64};
        std::mutex controlsMutex;
        MidiTransportControls controls;
    };
    std::unique_ptr<DeviceState> device;
#endif
};

MidiInputManager::MidiInputManager() : m_impl(std::make_unique<Impl>()) {}
MidiInputManager::~MidiInputManager() { closeAll(); }

std::vector<MidiInputManager::Device> MidiInputManager::enumerateInputDevices() {
    std::vector<Device> devices;
#if defined(HAVE_RTMIDI)
    try {
        RtMidiIn midiIn;
        unsigned int n = midiIn.getPortCount();
        for (unsigned int i = 0; i < n; ++i) {
            Device d;
            d.id = static_cast<int>(i);
            try {
                d.name = QString::fromStdString(midiIn.getPortName(i));
            } catch (...) {
                d.name = QString("MIDI Input %1").arg(i);
            }
            devices.push_back(d);
        }
    } catch (...) {
    }
#else
    (void)devices;
#endif
    return devices;
}

bool MidiInputManager::parseMessage(const std::vector<uint8_t>& bytes, MidiMessage& out) {
    if (bytes.empty())
        return false;
    uint8_t status = bytes[0];
    if ((status & 0x80) == 0)
        return false;
    out.status = status;
    out.data1 = bytes.size() > 1 ? bytes[1] : 0;
    out.data2 = bytes.size() > 2 ? bytes[2] : 0;
    out.sampleOffset = 0;
    return true;
}

#if defined(HAVE_RTMIDI)
void MidiInputManager::midiCallback(double /*deltaTime*/, std::vector<unsigned char>* message,
                                    void* userData) {
    auto* state = static_cast<Impl::DeviceState*>(userData);
    MidiMessage msg;
    if (!MidiInputManager::parseMessage(*message, msg))
        return;

    MidiTransportControls controls;
    {
        std::lock_guard<std::mutex> lock(state->controlsMutex);
        controls = state->controls;
    }

    if (controls.type != 0) {
        uint8_t value = 0;
        bool isTransport = false;
        if ((msg.status & 0xF0) == 0xB0) {
            uint8_t cc = msg.data1;
            if (cc == controls.play) value = static_cast<uint8_t>(MidiTransportCommand::Play);
            else if (cc == controls.stop) value = static_cast<uint8_t>(MidiTransportCommand::Stop);
            else if (cc == controls.record) value = static_cast<uint8_t>(MidiTransportCommand::Record);
            isTransport = (value != 0);
        } else if (controls.type == 2 && (msg.status & 0xF0) == 0x90) {
            if (msg.isNoteOn()) {
                uint8_t note = msg.data1;
                if (note == controls.play) value = static_cast<uint8_t>(MidiTransportCommand::Play);
                else if (note == controls.stop) value = static_cast<uint8_t>(MidiTransportCommand::Stop);
                else if (note == controls.record) value = static_cast<uint8_t>(MidiTransportCommand::Record);
            }
            isTransport = (value != 0);
        }
        if (isTransport) {
            state->transport.push(value);
            return;
        }
    }

    if (msg.isNoteOn() || msg.isNoteOff())
        state->notes.push(msg);
}
#endif

void MidiInputManager::open(int deviceId) {
#if defined(HAVE_RTMIDI)
    closeAll();
    if (deviceId < 0)
        return;
    auto state = std::make_unique<Impl::DeviceState>();
    {
        std::lock_guard<std::mutex> lock(m_impl->controlsMutex);
        state->controls = m_impl->controls;
    }
    try {
        state->in = std::make_unique<RtMidiIn>();
        state->in->openPort(static_cast<unsigned int>(deviceId), "vvvdaw input");
        state->in->ignoreTypes(false, false, false);
        state->in->setCallback(&MidiInputManager::midiCallback, state.get());
        m_impl->device = std::move(state);
    } catch (...) {
        m_impl->device.reset();
    }
#else
    (void)deviceId;
#endif
}

void MidiInputManager::close(int deviceId) {
    (void)deviceId;
    closeAll();
}

void MidiInputManager::closeAll() {
#if defined(HAVE_RTMIDI)
    if (m_impl->device) {
        try {
            m_impl->device->in->closePort();
        } catch (...) {
        }
        m_impl->device.reset();
    }
#endif
}

bool MidiInputManager::isActive() const {
#if defined(HAVE_RTMIDI)
    return m_impl->device != nullptr;
#else
    return false;
#endif
}

void MidiInputManager::setTransportControls(const MidiTransportControls& controls) {
    {
        std::lock_guard<std::mutex> lock(m_impl->controlsMutex);
        m_impl->controls = controls;
    }
#if defined(HAVE_RTMIDI)
    if (m_impl->device) {
        std::lock_guard<std::mutex> lock(m_impl->device->controlsMutex);
        m_impl->device->controls = controls;
    }
#endif
}

bool MidiInputManager::hasPendingNotes() const {
#if defined(HAVE_RTMIDI)
    return m_impl->device && !m_impl->device->notes.empty();
#else
    return false;
#endif
}

int MidiInputManager::pollNotes(MidiMessage* out, int maxCount) {
#if defined(HAVE_RTMIDI)
    if (!m_impl->device || maxCount <= 0)
        return 0;
    int n = 0;
    while (n < maxCount) {
        MidiMessage m;
        if (!m_impl->device->notes.pop(m))
            break;
        out[n++] = m;
    }
    return n;
#else
    (void)out;
    (void)maxCount;
    return 0;
#endif
}

int MidiInputManager::pollTransport(MidiTransportCommand* out, int maxCount) {
#if defined(HAVE_RTMIDI)
    if (!m_impl->device || maxCount <= 0)
        return 0;
    int n = 0;
    while (n < maxCount) {
        uint8_t v = 0;
        if (!m_impl->device->transport.pop(v))
            break;
        out[n] = static_cast<MidiTransportCommand>(v);
        if (out[n] != MidiTransportCommand::None)
            ++n;
    }
    return n;
#else
    (void)out;
    (void)maxCount;
    return 0;
#endif
}