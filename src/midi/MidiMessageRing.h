#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

// Lock-free single-producer / single-consumer ring buffer for fixed-size
// elements (MIDI messages, timestamped notes, ...). The producer is typically
// the RtMidi callback thread, the consumer the audio or GUI thread.
template <typename T>
class MidiRingBuffer {
public:
    explicit MidiRingBuffer(size_t capacity = 4096)
        : m_capacity(capacity)
        , m_data(capacity)
    {
    }

    // Producer side (real-time safe).
    size_t push(const T& value) {
        size_t wp = m_writePos.load(std::memory_order_relaxed);
        size_t rp = m_readPos.load(std::memory_order_acquire);
        if (full(wp, rp))
            return 0;
        m_data[wp] = value;
        m_writePos.store(next(wp), std::memory_order_release);
        return 1;
    }

    // Consumer side (real-time safe).
    size_t pop(T& out) {
        size_t rp = m_readPos.load(std::memory_order_relaxed);
        size_t wp = m_writePos.load(std::memory_order_acquire);
        if (rp == wp)
            return 0;
        out = m_data[rp];
        m_readPos.store(next(rp), std::memory_order_release);
        return 1;
    }

    size_t used() const {
        size_t wp = m_writePos.load(std::memory_order_acquire);
        size_t rp = m_readPos.load(std::memory_order_acquire);
        return (wp >= rp) ? (wp - rp) : (m_capacity - rp + wp);
    }

    bool empty() const { return used() == 0; }

    size_t capacity() const { return m_capacity; }

    void reset() {
        m_writePos.store(0, std::memory_order_release);
        m_readPos.store(0, std::memory_order_release);
    }

private:
    size_t next(size_t pos) const { return (pos + 1) % m_capacity; }
    bool full(size_t wp, size_t rp) const {
        return next(wp) == rp;
    }

    size_t m_capacity;
    std::vector<T> m_data;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};
};