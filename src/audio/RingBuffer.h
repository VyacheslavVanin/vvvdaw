#pragma once
#include <atomic>
#include <cstring>
#include <vector>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 32768)
        : data(capacity, 0.0f)
    {
    }

    RingBuffer(RingBuffer&& other) noexcept
        : data(std::move(other.data))
        , writePos(other.writePos.load(std::memory_order_relaxed))
        , readPos(other.readPos.load(std::memory_order_relaxed))
    {
        other.writePos.store(0, std::memory_order_relaxed);
        other.readPos.store(0, std::memory_order_relaxed);
    }

    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            data = std::move(other.data);
            writePos.store(other.writePos.load(std::memory_order_relaxed), std::memory_order_relaxed);
            readPos.store(other.readPos.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.writePos.store(0, std::memory_order_relaxed);
            other.readPos.store(0, std::memory_order_relaxed);
        }
        return *this;
    }

    size_t write(const float* samples, size_t count) {
        size_t wp = writePos.load(std::memory_order_relaxed);
        size_t rp = readPos.load(std::memory_order_acquire);
        size_t used = (wp >= rp) ? (wp - rp) : (capacity() - rp + wp);
        size_t freeSpace = capacity() - used - 1;
        size_t toWrite = std::min(count, freeSpace);

        for (size_t i = 0; i < toWrite; ++i)
            data[(wp + i) % capacity()] = samples[i];

        writePos.store((wp + toWrite) % capacity(), std::memory_order_release);
        return toWrite;
    }

    size_t read(float* dest, size_t maxCount) {
        size_t rp = readPos.load(std::memory_order_relaxed);
        size_t wp = writePos.load(std::memory_order_acquire);

        size_t avail;
        if (wp >= rp)
            avail = wp - rp;
        else
            avail = capacity() - rp + wp;

        size_t toRead = std::min(avail, maxCount);
        if (toRead == 0) return 0;

        if (rp + toRead <= capacity()) {
            std::memcpy(dest, data.data() + rp, toRead * sizeof(float));
        } else {
            size_t firstPart = capacity() - rp;
            std::memcpy(dest, data.data() + rp, firstPart * sizeof(float));
            std::memcpy(dest + firstPart, data.data(), (toRead - firstPart) * sizeof(float));
        }

        readPos.store((rp + toRead) % capacity(), std::memory_order_release);
        return toRead;
    }

    size_t available() const {
        size_t rp = readPos.load(std::memory_order_acquire);
        size_t wp = writePos.load(std::memory_order_acquire);
        if (wp >= rp) return wp - rp;
        return capacity() - rp + wp;
    }

    size_t capacity() const { return data.size(); }

    void reset() {
        writePos.store(0, std::memory_order_release);
        readPos.store(0, std::memory_order_release);
    }

    std::vector<float> data;
    std::atomic<size_t> writePos{0};
    std::atomic<size_t> readPos{0};
};
