#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    bool push(const T& item) {
        const auto head = m_head.load(std::memory_order_relaxed);
        const auto next = head + 1;
        if (next - m_tail.load(std::memory_order_acquire) > Capacity)
            return false;
        m_buffer[head & (Capacity - 1)] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        const auto head = m_head.load(std::memory_order_relaxed);
        const auto next = head + 1;
        if (next - m_tail.load(std::memory_order_acquire) > Capacity)
            return false;
        m_buffer[head & (Capacity - 1)] = std::move(item);
        m_head.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        if (tail >= m_head.load(std::memory_order_acquire))
            return std::nullopt;
        T item = std::move(m_buffer[tail & (Capacity - 1)]);
        m_tail.store(tail + 1, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return m_head.load(std::memory_order_acquire) <=
               m_tail.load(std::memory_order_acquire);
    }

private:
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
    T m_buffer[Capacity];
};
