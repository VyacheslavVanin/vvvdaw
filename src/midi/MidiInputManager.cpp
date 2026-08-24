#include "MidiInputManager.h"
#include <mutex>

#if defined(HAVE_RTMIDI)
#include <RtMidi.h>
#endif

namespace {

struct LearnCapture {
    uint8_t target = 0;
    uint8_t type = 0;    // 1 = CC, 2 = other channel voice message
    uint8_t kind = 0;    // status & 0xF0 of the learned message
    uint8_t channel = 0;
    uint8_t value = 0;
};

} // namespace

struct MidiInputManager::Impl {
    std::mutex controlsMutex;
    MidiTransportControls controls;
    std::mutex deviceMutex;

#if defined(HAVE_RTMIDI)
    struct DeviceState {
        std::unique_ptr<RtMidiIn> in;
        MidiRingBuffer<MidiMessage> notes{4096};
        MidiRingBuffer<uint8_t> transport{64};
        MidiRingBuffer<LearnCapture> learned{8};
        std::atomic<MidiLearnTarget> learnTarget{MidiLearnTarget::None};
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

MidiTransportCommand MidiInputManager::matchTransport(const MidiMessage& msg,
                                                      const MidiTransportControls& controls) {
    if (controls.type == 0)
        return MidiTransportCommand::None;
    // Restrict to the configured channel (when one is set).
    if (controls.channel >= 0 && controls.channel <= 15
        && (msg.status & 0x0F) != static_cast<uint8_t>(controls.channel))
        return MidiTransportCommand::None;

    // A note-on with velocity 0 is a note-off; treat the two identically so a
    // release (note-on vel 0 or explicit note-off) never re-triggers transport.
    int msgKind = msg.status & 0xF0;
    if (msgKind == 0x90 && !msg.isNoteOn())
        msgKind = 0x80;

    // The exact message kind learned (or derived from the coarse type).
    int kind = controls.kind;
    if (kind == 0)
        kind = (controls.type == 1) ? 0xB0 : 0x90;
    if (msgKind != kind)
        return MidiTransportCommand::None;

    // For note-on, only a real press (velocity > 0) triggers; for note-off
    // (0x80) only the release message itself triggers. Other channel voice
    // messages (poly pressure, program change, channel pressure, CC) are
    // one-shot presses.
    uint8_t v = msg.data1;
    if (v == controls.play) return MidiTransportCommand::Play;
    if (v == controls.stop) return MidiTransportCommand::Stop;
    if (v == controls.record) return MidiTransportCommand::Record;
    return MidiTransportCommand::None;
}

bool MidiInputManager::isLearnable(const MidiMessage& msg) {
    uint8_t kind = msg.status & 0xF0;
    // Any channel voice message carrying a value in data1 (note on/off, poly
    // pressure, CC, program change, channel pressure) is a learn candidate.
    return kind >= 0x80 && kind <= 0xDF;
}

#if defined(HAVE_RTMIDI)
void MidiInputManager::midiCallback(double /*deltaTime*/, std::vector<unsigned char>* message,
                                    void* userData) {
    auto* state = static_cast<Impl::DeviceState*>(userData);
    MidiMessage msg;
    if (!MidiInputManager::parseMessage(*message, msg))
        return;

    // Learn mode: capture the first channel voice message and consume it (one
    // press = one capture, and the message does not also reach the
    // note/transport rings).
    MidiLearnTarget learn = state->learnTarget.load(std::memory_order_relaxed);
    if (learn != MidiLearnTarget::None && isLearnable(msg)) {
        LearnCapture c;
        c.target = static_cast<uint8_t>(learn);
        c.type = ((msg.status & 0xF0) == 0xB0) ? 1 : 2;
        c.kind = msg.status & 0xF0;
        // A note-on with velocity 0 is a note-off; store the normalized kind so
        // matching (matchTransport) accepts the same message family.
        if (c.kind == 0x90 && !msg.isNoteOn())
            c.kind = 0x80;
        c.channel = msg.status & 0x0F;
        c.value = msg.data1;
        state->learned.push(c);
        state->learnTarget.store(MidiLearnTarget::None, std::memory_order_relaxed);
        return;
    }

    MidiTransportControls controls;
    {
        std::lock_guard<std::mutex> lock(state->controlsMutex);
        controls = state->controls;
    }

    MidiTransportCommand cmd = matchTransport(msg, controls);
    if (cmd != MidiTransportCommand::None) {
        state->transport.push(static_cast<uint8_t>(cmd));
        return;
    }

    // Every remaining channel voice message (notes, CC, pitch bend, pressure,
    // program change) is delivered to the audio thread for recording/preview.
    if (msg.isChannelVoice())
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
        std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
        m_impl->device = std::move(state);
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
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
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
    if (m_impl->device) {
        std::lock_guard<std::mutex> cl(m_impl->device->controlsMutex);
        m_impl->device->controls = controls;
    }
#endif
}

void MidiInputManager::setLearnTarget(MidiLearnTarget target) {
#if defined(HAVE_RTMIDI)
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
    if (m_impl->device)
        m_impl->device->learnTarget.store(target, std::memory_order_relaxed);
#else
    (void)target;
#endif
}

MidiLearnTarget MidiInputManager::learnTarget() const {
#if defined(HAVE_RTMIDI)
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
    return m_impl->device ? m_impl->device->learnTarget.load(std::memory_order_relaxed)
                          : MidiLearnTarget::None;
#else
    return MidiLearnTarget::None;
#endif
}

bool MidiInputManager::popLearned(MidiTransportControls& out) {
#if defined(HAVE_RTMIDI)
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
    if (!m_impl->device)
        return false;
    LearnCapture c;
    if (!m_impl->device->learned.pop(c))
        return false;
    out.type = c.type;
    out.kind = c.kind;
    out.channel = c.channel;
    switch (static_cast<MidiLearnTarget>(c.target)) {
    case MidiLearnTarget::Play: out.play = c.value; break;
    case MidiLearnTarget::Record: out.record = c.value; break;
    case MidiLearnTarget::Stop: out.stop = c.value; break;
    default: return false;
    }
    return true;
#else
    (void)out;
    return false;
#endif
}

bool MidiInputManager::hasPendingNotes() const {
#if defined(HAVE_RTMIDI)
    std::unique_lock<std::mutex> lock(m_impl->deviceMutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_impl->device)
        return false;
    return !m_impl->device->notes.empty();
#else
    return false;
#endif
}

int MidiInputManager::pollNotes(MidiMessage* out, int maxCount) {
#if defined(HAVE_RTMIDI)
    std::unique_lock<std::mutex> lock(m_impl->deviceMutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_impl->device || maxCount <= 0)
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
    std::lock_guard<std::mutex> lock(m_impl->deviceMutex);
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