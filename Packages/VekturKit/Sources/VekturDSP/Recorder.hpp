// Recorder.hpp — Phase 9 audio recording tap: a wait-free single-producer /
// single-consumer ring of interleaved stereo frames. The render thread
// writes the kernel's final output; a [ui]/worker thread drains it to a file
// (WAV/AIFF/CAF in Swift via AVAudioFile; WAV in the web build). Storage is
// allocated once, on the first start (never on the render thread), and kept.
// If the reader falls behind, whole frames are dropped and counted — the
// audio thread never waits.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace vektur {

class AudioRecorder {
public:
    static constexpr uint32_t kCapacityFrames = 1u << 19;   // 10.9 s at 48 kHz

    // [ui] Allocates on first use, resets counters and arms the tap.
    bool start() {
        if (!buffer_) {
            buffer_.reset(new (std::nothrow) float[kCapacityFrames * 2]);
            if (!buffer_) return false;
        }
        recording_.store(false, std::memory_order_release);
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
        dropped_.store(0, std::memory_order_relaxed);
        written_.store(0, std::memory_order_relaxed);
        recording_.store(true, std::memory_order_release);
        return true;
    }
    void stop() noexcept { recording_.store(false, std::memory_order_release); }   // [ui]; read() still drains
    bool isRecording() const noexcept { return recording_.load(std::memory_order_acquire); }

    // [render]
    void write(const float* l, const float* r, uint32_t frames) noexcept {
        if (!recording_.load(std::memory_order_acquire)) return;
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        const uint64_t space = kCapacityFrames - (head - tail);
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(frames, space));
        float* b = buffer_.get();
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t at = ((head + i) & (kCapacityFrames - 1)) * 2;
            b[at] = l[i]; b[at + 1] = r[i];
        }
        head_.store(head + n, std::memory_order_release);
        written_.fetch_add(n, std::memory_order_relaxed);
        if (n < frames) dropped_.fetch_add(frames - n, std::memory_order_relaxed);
    }

    // [ui] Copies up to maxFrames interleaved stereo frames; returns frames.
    uint32_t read(float* out, uint32_t maxFrames) noexcept {
        if (!buffer_) return 0;
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const uint64_t head = head_.load(std::memory_order_acquire);
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(head - tail, maxFrames));
        const float* b = buffer_.get();
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t at = ((tail + i) & (kCapacityFrames - 1)) * 2;
            out[2 * i] = b[at]; out[2 * i + 1] = b[at + 1];
        }
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    uint64_t written() const noexcept { return written_.load(std::memory_order_relaxed); }

private:
    std::unique_ptr<float[]> buffer_;
    std::atomic<bool> recording_{false};
    std::atomic<uint64_t> head_{0}, tail_{0}, dropped_{0}, written_{0};
};

} // namespace vektur
