#include "MidiOutputManager.h"
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef HAVE_RTMIDI
#include <RtMidi.h>
#else
class RtMidiOut { public: void closePort() {} };
#endif

struct MidiOutputManager::Impl {
    std::mutex mutex;
    std::unordered_map<int, std::unique_ptr<RtMidiOut>> outputs;
};

MidiOutputManager::MidiOutputManager() : m_impl(std::make_unique<Impl>()) {}
MidiOutputManager::~MidiOutputManager() { closeAll(); }

std::vector<MidiOutputManager::Device> MidiOutputManager::enumerateOutputDevices() {
    std::vector<Device> devices;
#ifdef HAVE_RTMIDI
    try {
        RtMidiOut midiOut;
        unsigned int n = midiOut.getPortCount();
        for (unsigned int i = 0; i < n; ++i) {
            Device d;
            d.id = static_cast<int>(i);
            try {
                d.name = QString::fromStdString(midiOut.getPortName(i));
            } catch (...) {
                d.name = QString("MIDI %1").arg(i);
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

void MidiOutputManager::open(int deviceId) {
#ifdef HAVE_RTMIDI
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->outputs.count(deviceId) > 0)
        return;
    auto out = std::make_unique<RtMidiOut>();
    try {
        out->openPort(static_cast<unsigned int>(deviceId), "vvvdaw");
        m_impl->outputs[deviceId] = std::move(out);
    } catch (...) {
    }
#else
    (void)deviceId;
#endif
}

void MidiOutputManager::close(int deviceId) {
#ifdef HAVE_RTMIDI
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto it = m_impl->outputs.find(deviceId);
    if (it != m_impl->outputs.end()) {
        try {
            it->second->closePort();
        } catch (...) {
        }
        m_impl->outputs.erase(it);
    }
#else
    (void)deviceId;
#endif
}

void MidiOutputManager::closeAll() {
#ifdef HAVE_RTMIDI
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& [id, out] : m_impl->outputs) {
        try {
            out->closePort();
        } catch (...) {
        }
    }
    m_impl->outputs.clear();
#else
#endif
}

void MidiOutputManager::send(int deviceId, uint8_t status, uint8_t data1, uint8_t data2) {
#ifdef HAVE_RTMIDI
    std::unique_lock<std::mutex> lock(m_impl->mutex, std::try_to_lock);
    if (!lock)
        return;
    auto it = m_impl->outputs.find(deviceId);
    if (it == m_impl->outputs.end())
        return;
    try {
        std::vector<unsigned char> msg = { status, data1, data2 };
        it->second->sendMessage(&msg);
    } catch (...) {
    }
#else
    (void)deviceId; (void)status; (void)data1; (void)data2;
#endif
}

void MidiOutputManager::sendAllNotesOff(int deviceId) {
    for (int ch = 0; ch < 16; ++ch)
        send(deviceId, static_cast<uint8_t>(0xB0 | ch), 0x7B, 0);
}
