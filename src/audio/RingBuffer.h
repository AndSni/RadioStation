#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace radio::audio {

// Lock-free single-producer/single-consumer ring buffer of interleaved
// float PCM frames (one frame = channelCount samples). Built specifically
// to bridge a GStreamer appsink's "new-sample" callback (the producer,
// running on a GStreamer streaming thread) into a real-time audio callback
// (the consumer — miniaudio's data_callback, which must never block or
// take a lock). write()/read() only ever touch std::atomic indices with
// acquire/release ordering; neither side can block the other.
//
// Capacity is rounded up to a power of two so index wraparound is a cheap
// mask instead of a modulo. Not safe for more than one concurrent writer
// or more than one concurrent reader — by design, this bridges exactly one
// producer thread to exactly one consumer thread.
class RingBuffer {
public:
    RingBuffer(size_t capacityFrames, int channels)
        : m_channels(channels)
        , m_capacityFrames(nextPowerOfTwo(capacityFrames))
        , m_mask(m_capacityFrames - 1)
        , m_buffer(static_cast<size_t>(m_capacityFrames) * channels)
    {
    }

    int channels() const { return m_channels; }
    size_t capacityFrames() const { return m_capacityFrames; }

    // Returns the number of frames actually written (less than frameCount
    // if the buffer doesn't have enough free space — the producer must
    // decide how to handle a partial/zero write, e.g. drop the remainder).
    size_t write(const float* interleaved, size_t frameCount)
    {
        const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        const size_t readIdx = m_readIndex.load(std::memory_order_acquire);
        const size_t free = m_capacityFrames - (writeIdx - readIdx);
        const size_t toWrite = std::min(frameCount, free);
        if (toWrite == 0)
            return 0;

        const size_t start = writeIdx & m_mask;
        const size_t firstRun = std::min(toWrite, m_capacityFrames - start);
        std::memcpy(&m_buffer[start * m_channels], interleaved, firstRun * m_channels * sizeof(float));
        if (toWrite > firstRun) {
            std::memcpy(&m_buffer[0], interleaved + firstRun * m_channels,
                (toWrite - firstRun) * m_channels * sizeof(float));
        }

        m_writeIndex.store(writeIdx + toWrite, std::memory_order_release);
        return toWrite;
    }

    // Returns the number of frames actually read (less than frameCount if
    // the buffer is running dry — the consumer must fill the remainder
    // with silence itself, never block waiting for more).
    size_t read(float* outInterleaved, size_t frameCount)
    {
        const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        const size_t available = writeIdx - readIdx;
        const size_t toRead = std::min(frameCount, available);
        if (toRead == 0)
            return 0;

        const size_t start = readIdx & m_mask;
        const size_t firstRun = std::min(toRead, m_capacityFrames - start);
        std::memcpy(outInterleaved, &m_buffer[start * m_channels], firstRun * m_channels * sizeof(float));
        if (toRead > firstRun) {
            std::memcpy(outInterleaved + firstRun * m_channels, &m_buffer[0],
                (toRead - firstRun) * m_channels * sizeof(float));
        }

        m_readIndex.store(readIdx + toRead, std::memory_order_release);
        return toRead;
    }

    size_t availableToRead() const
    {
        return m_writeIndex.load(std::memory_order_acquire) - m_readIndex.load(std::memory_order_relaxed);
    }

    size_t availableToWrite() const { return m_capacityFrames - availableToRead(); }

    // Only safe when neither producer nor consumer is concurrently active
    // (e.g. before a source is attached to the mix graph) — not itself
    // synchronized against write()/read().
    void clear()
    {
        m_readIndex.store(0, std::memory_order_relaxed);
        m_writeIndex.store(0, std::memory_order_relaxed);
    }

private:
    static size_t nextPowerOfTwo(size_t n)
    {
        size_t p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    int m_channels;
    size_t m_capacityFrames;
    size_t m_mask;
    std::vector<float> m_buffer;
    std::atomic<size_t> m_writeIndex{ 0 };
    std::atomic<size_t> m_readIndex{ 0 };
};

} // namespace radio::audio
