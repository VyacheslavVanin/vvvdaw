#pragma once
#include <atomic>
#include <cstring>
#include <vector>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 32768)
        : m_data(capacity, 0.0f)
    {
    }

    RingBuffer(RingBuffer&& other) noexcept
        : m_data(std::move(other.m_data))
        , m_writePos(other.m_writePos.load(std::memory_order_relaxed))
        , m_readPos(other.m_readPos.load(std::memory_order_relaxed))
    {
        other.m_writePos.store(0, std::memory_order_relaxed);
        other.m_readPos.store(0, std::memory_order_relaxed);
    }

    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            m_data = std::move(other.m_data);
            m_writePos.store(other.m_writePos.load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_readPos.store(other.m_readPos.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.m_writePos.store(0, std::memory_order_relaxed);
            other.m_readPos.store(0, std::memory_order_relaxed);
        }
        return *this;
    }

    size_t write(const float* samples, size_t count) {
        size_t wp = m_writePos.load(std::memory_order_relaxed);
        size_t rp = m_readPos.load(std::memory_order_acquire);
        size_t toWrite = std::min(count, freeSpace(wp, rp));

        for (size_t i = 0; i < toWrite; ++i)
            m_data[(wp + i) % capacity()] = samples[i];

        m_writePos.store((wp + toWrite) % capacity(), std::memory_order_release);
        return toWrite;
    }

    size_t read(float* dest, size_t maxCount) {
        size_t rp = m_readPos.load(std::memory_order_relaxed);
        size_t wp = m_writePos.load(std::memory_order_acquire);

        size_t toRead = std::min(used(wp, rp), maxCount);
        if (toRead == 0) return 0;

        if (rp + toRead <= capacity()) {
            std::memcpy(dest, m_data.data() + rp, toRead * sizeof(float));
        } else {
            size_t firstPart = capacity() - rp;
            std::memcpy(dest, m_data.data() + rp, firstPart * sizeof(float));
            std::memcpy(dest + firstPart, m_data.data(), (toRead - firstPart) * sizeof(float));
        }

        m_readPos.store((rp + toRead) % capacity(), std::memory_order_release);
        return toRead;
    }

    size_t used() const {
        return used(m_writePos.load(std::memory_order_acquire),
                    m_readPos.load(std::memory_order_acquire));
    }

    // Free slots that can still be written without overwriting unread data
    // (keeps one slot empty to distinguish full from empty).
    size_t freeSpace() const {
        return freeSpace(m_writePos.load(std::memory_order_relaxed),
                         m_readPos.load(std::memory_order_acquire));
    }

    size_t capacity() const { return m_data.size(); }

    void reset() {
        m_writePos.store(0, std::memory_order_release);
        m_readPos.store(0, std::memory_order_release);
    }

private:
    size_t used(size_t wp, size_t rp) const {
        return (wp >= rp) ? (wp - rp) : (capacity() - rp + wp);
    }

    size_t freeSpace(size_t wp, size_t rp) const {
        return capacity() - used(wp, rp) - 1;
    }

    std::vector<float> m_data;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};
};
