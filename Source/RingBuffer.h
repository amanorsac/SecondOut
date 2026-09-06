#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

/**
    Lock-free single-producer / single-consumer ring buffer of interleaved
    stereo frames (2 floats per frame).

    Producer: the host's audio thread (processBlock).
    Consumer: the secondary device's WASAPI callback thread.

    No locks, no allocations after prepare(). Capacity is rounded up to a
    power of two so index wrapping is a mask.
*/
class RingBuffer
{
public:
    static constexpr int numChannels = 2;

    RingBuffer() = default;

    /** Not thread-safe: call only while neither thread is using the buffer. */
    void prepare (int capacityFrames)
    {
        capacity = nextPow2 (capacityFrames);
        mask     = capacity - 1;
        storage.assign (static_cast<size_t> (capacity) * numChannels, 0.0f);
        writePos.store (0, std::memory_order_relaxed);
        readPos.store (0, std::memory_order_relaxed);
    }

    int getCapacity() const noexcept { return capacity; }

    /** Frames currently readable. Safe from either thread (approximate from the other side). */
    int getNumReady() const noexcept
    {
        return static_cast<int> (writePos.load (std::memory_order_acquire)
                               - readPos.load (std::memory_order_acquire));
    }

    int getFreeSpace() const noexcept { return capacity - getNumReady(); }

    /** Producer side: write planar channel data. Returns frames actually written
        (fewer than requested if the buffer is full — excess is dropped). */
    int write (const float* left, const float* right, int numFrames) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);
        const int free = capacity - static_cast<int> (w - r);
        const int n = numFrames < free ? numFrames : free;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<size_t> ((w + static_cast<uint64_t> (i)) & static_cast<uint64_t> (mask)) * numChannels;
            storage[idx]     = left[i];
            storage[idx + 1] = right != nullptr ? right[i] : left[i];
        }

        writePos.store (w + static_cast<uint64_t> (n), std::memory_order_release);
        return n;
    }

    /** Consumer side: read up to numFrames into planar buffers. Returns frames read. */
    int read (float* left, float* right, int numFrames) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        const int avail = static_cast<int> (w - r);
        const int n = numFrames < avail ? numFrames : avail;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<size_t> ((r + static_cast<uint64_t> (i)) & static_cast<uint64_t> (mask)) * numChannels;
            left[i]  = storage[idx];
            right[i] = storage[idx + 1];
        }

        readPos.store (r + static_cast<uint64_t> (n), std::memory_order_release);
        return n;
    }

    /** Consumer side: discard up to numFrames. Returns frames discarded. */
    int discard (int numFrames) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        const int avail = static_cast<int> (w - r);
        const int n = numFrames < avail ? numFrames : avail;
        readPos.store (r + static_cast<uint64_t> (n), std::memory_order_release);
        return n;
    }

    /** Consumer side: drop everything currently buffered. */
    void flushFromConsumer() noexcept
    {
        readPos.store (writePos.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    static int nextPow2 (int v) noexcept
    {
        int p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    std::vector<float> storage;
    int capacity = 0;
    int mask = 0;

    // Monotonic 64-bit counters — never wrap in practice, index = counter & mask.
    alignas (64) std::atomic<uint64_t> writePos { 0 };
    alignas (64) std::atomic<uint64_t> readPos  { 0 };
};
