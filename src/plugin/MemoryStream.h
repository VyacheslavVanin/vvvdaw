#pragma once
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/funknownimpl.h>
#include <vector>
#include <cstring>

namespace vvvdaw {

class StateStream : public Steinberg::IBStream {
public:
    StateStream();
    ~StateStream();

    StateStream(void* memory, Steinberg::TSize memorySize) {
        if (memory && memorySize > 0) {
            m_buffer.resize(static_cast<size_t>(memorySize));
            std::memcpy(m_buffer.data(), memory, static_cast<size_t>(memorySize));
        }
    }

    Steinberg::tresult PLUGIN_API read(void* data, Steinberg::int32 numBytes,
                                       Steinberg::int32* numBytesRead) override {
        if (m_pos + numBytes > static_cast<Steinberg::int64>(m_buffer.size()))
            numBytes = static_cast<Steinberg::int32>(
                static_cast<Steinberg::int64>(m_buffer.size()) - m_pos);
        if (numBytes > 0)
            std::memcpy(data, m_buffer.data() + m_pos, static_cast<size_t>(numBytes));
        m_pos += numBytes;
        if (numBytesRead) *numBytesRead = numBytes;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API write(void* data, Steinberg::int32 numBytes,
                                        Steinberg::int32* numBytesWritten) override {
        if (m_pos + numBytes > static_cast<Steinberg::int64>(m_buffer.size()))
            m_buffer.resize(static_cast<size_t>(m_pos + numBytes));
        std::memcpy(m_buffer.data() + m_pos, data, static_cast<size_t>(numBytes));
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 p, Steinberg::int32 mode,
                                        Steinberg::int64* result) override {
        if (mode == kIBSeekSet) m_pos = p;
        else if (mode == kIBSeekCur) m_pos += p;
        else if (mode == kIBSeekEnd)
            m_pos = static_cast<Steinberg::int64>(m_buffer.size()) + p;
        if (m_pos < 0) m_pos = 0;
        if (m_pos > static_cast<Steinberg::int64>(m_buffer.size()))
            m_pos = static_cast<Steinberg::int64>(m_buffer.size());
        if (result) *result = m_pos;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* p) override {
        if (p) *p = m_pos;
        return Steinberg::kResultTrue;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 0; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IBStream::iid)) {
            *obj = static_cast<Steinberg::IBStream*>(this);
            return Steinberg::kResultTrue;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::TSize getSize() const { return static_cast<Steinberg::TSize>(m_buffer.size()); }
    const char* getData() const { return m_buffer.data(); }
    void reset() { m_pos = 0; }

private:
    std::vector<char> m_buffer;
    Steinberg::int64 m_pos = 0;
};

} // namespace vvvdaw
