// AURenderAdapter.mm — converts the host's AURenderEvent list into VKEvents
// and renders into the host AudioBufferList. Objective-C++ only because the
// AURenderEvent types live in the Objective-C AUAudioUnit header; no
// Objective-C objects are touched on the render thread.
#if defined(__APPLE__)

#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUAudioUnit.h>
#import <CoreMIDI/CoreMIDI.h>

#include "VekturDSP.h"
#include "Kernel.hpp"

#include <algorithm>
#include <cstdint>

using vektur::Kernel;

namespace {

inline uint32_t offsetFor(AUEventSampleTime eventTime, double bufferStart, uint32_t frames) {
    if (eventTime == AUEventSampleTimeImmediate) return 0;
    const double rel = static_cast<double>(eventTime) - bufferStart;
    if (rel <= 0.0) return 0;
    return std::min(static_cast<uint32_t>(rel), frames == 0 ? 0u : frames - 1);
}

// UMP → MIDI 1.0 bytes for system (type 0x1) and MIDI 1.0 channel voice
// (type 0x2) packets. MIDI 2.0 channel voice (type 0x4) is passed to the
// kernel whole (VK_EVENT_UMP) so 16/32-bit resolution and per-note messages
// survive; the down-conversion below is kept as a fallback for tests.
inline int umpToMidi1(const uint32_t* w, uint8_t out[3]) {
    const uint32_t type = w[0] >> 28;
    if (type == 0x1) {
        out[0] = (w[0] >> 16) & 0xFF; out[1] = (w[0] >> 8) & 0x7F; out[2] = w[0] & 0x7F;
        return out[0] == 0xF2 ? 3 : (out[0] == 0xF1 || out[0] == 0xF3 ? 2 : 1);
    }
    if (type == 0x2) {
        out[0] = (w[0] >> 16) & 0xFF; out[1] = (w[0] >> 8) & 0x7F; out[2] = w[0] & 0x7F;
        const uint8_t s = out[0] & 0xF0;
        return (s == 0xC0 || s == 0xD0) ? 2 : 3;
    }
    if (type == 0x4) {
        const uint8_t status = (w[0] >> 16) & 0xFF;
        const uint8_t s = status & 0xF0;
        const uint8_t idx = (w[0] >> 8) & 0x7F;
        if (s == 0x90 || s == 0x80) {
            uint8_t vel = static_cast<uint8_t>((w[1] >> 25) & 0x7F);   // 16-bit → 7-bit
            if (s == 0x90 && vel == 0) vel = 1;                          // MIDI 2.0 vel 0 is still a note-on
            out[0] = status; out[1] = idx; out[2] = vel; return 3;
        }
        if (s == 0xB0) { out[0] = status; out[1] = idx; out[2] = static_cast<uint8_t>(w[1] >> 25); return 3; }
        if (s == 0xE0) {
            const uint32_t v14 = w[1] >> 18;
            out[0] = status; out[1] = v14 & 0x7F; out[2] = (v14 >> 7) & 0x7F; return 3;
        }
    }
    return 0;
}

inline uint32_t umpWordCount(uint32_t firstWord) {
    static const uint8_t sizes[16] = {1,1,1,2,2,4,1,1,2,2,2,3,3,4,4,4};
    return sizes[firstWord >> 28];
}

} // namespace

extern "C" int32_t vk_au_render(VKKernel* handle, uint32_t* actionFlags, double timestampSampleTime,
                                uint32_t frameCount, void* outputData, const void* realtimeEventListHead) {
    (void)actionFlags;
    auto* k = reinterpret_cast<Kernel*>(handle);
    auto* abl = static_cast<AudioBufferList*>(outputData);
    if (!k || !abl) return kAudioUnitErr_Uninitialized;
    if (!k->isAllocated()) return kAudioUnitErr_Uninitialized;
    if (frameCount > k->maxFrames()) return kAudioUnitErr_TooManyFramesToProcess;

    // Hosts may pass null mData and expect us to provide buffers.
    float* channels[VK_MAX_CHANNELS] = {nullptr, nullptr};
    const uint32_t channelCount = std::min<uint32_t>(abl->mNumberBuffers, VK_MAX_CHANNELS);
    for (uint32_t c = 0; c < channelCount; ++c) {
        if (abl->mBuffers[c].mData == nullptr) abl->mBuffers[c].mData = k->scratchChannel(c);
        abl->mBuffers[c].mDataByteSize = frameCount * sizeof(float);
        channels[c] = static_cast<float*>(abl->mBuffers[c].mData);
    }

    // Convert events into the kernel's preallocated array.
    VKEvent* events = k->eventScratch();
    uint32_t count = 0;
    for (auto* ev = static_cast<const AURenderEvent*>(realtimeEventListHead); ev; ev = ev->head.next) {
        if (count >= VK_MAX_EVENTS_PER_RENDER) { k->flagEventsDropped(); break; }
        switch (ev->head.eventType) {
            case AURenderEventParameter:
            case AURenderEventParameterRamp: {
                const AUParameterEvent& p = ev->parameter;
                VKEvent& out = events[count++];
                out = {};
                out.sampleOffset = offsetFor(p.eventSampleTime, timestampSampleTime, frameCount);
                out.type = ev->head.eventType == AURenderEventParameterRamp ? VK_EVENT_PARAM_RAMP : VK_EVENT_PARAM;
                out.address = p.parameterAddress;
                out.value = p.value;
                out.rampFrames = p.rampDurationSampleFrames;
                break;
            }
            case AURenderEventMIDI: {
                const AUMIDIEvent& m = ev->MIDI;
                VKEvent& out = events[count++];
                out = {};
                out.sampleOffset = offsetFor(m.eventSampleTime, timestampSampleTime, frameCount);
                out.type = VK_EVENT_MIDI1;
                out.length = static_cast<uint8_t>(std::min<uint16_t>(m.length, 3));
                for (int i = 0; i < out.length; ++i) out.data[i] = m.data[i];
                break;
            }
            case AURenderEventMIDIEventList: {
                const AUMIDIEventList& ml = ev->MIDIEventsList;
                const MIDIEventPacket* pkt = &ml.eventList.packet[0];
                for (UInt32 p = 0; p < ml.eventList.numPackets && count < VK_MAX_EVENTS_PER_RENDER; ++p) {
                    for (UInt32 w = 0; w < pkt->wordCount; ) {
                        if ((pkt->words[w] >> 28) == 0x4 && w + 1 < pkt->wordCount && count < VK_MAX_EVENTS_PER_RENDER) {
                            VKEvent& out = events[count++];
                            out = {};
                            out.sampleOffset = offsetFor(ml.eventSampleTime, timestampSampleTime, frameCount);
                            out.type = VK_EVENT_UMP;
                            out.ump[0] = pkt->words[w]; out.ump[1] = pkt->words[w + 1];
                            w += 2;
                            continue;
                        }
                        uint8_t bytes[3];
                        const int len = umpToMidi1(&pkt->words[w], bytes);
                        if (len > 0 && count < VK_MAX_EVENTS_PER_RENDER) {
                            VKEvent& out = events[count++];
                            out = {};
                            out.sampleOffset = offsetFor(ml.eventSampleTime, timestampSampleTime, frameCount);
                            out.type = VK_EVENT_MIDI1;
                            out.length = static_cast<uint8_t>(len);
                            out.data[0] = bytes[0]; out.data[1] = bytes[1]; out.data[2] = bytes[2];
                        }
                        w += umpWordCount(pkt->words[w]);
                    }
                    pkt = MIDIEventPacketNext(pkt);
                }
                break;
            }
            default: break;   // SysEx and future event types ignored for now.
        }
    }

    // AU lists are time-ordered, but MIDI-event-list expansion can interleave;
    // insertion sort is O(n) on already-sorted input and never allocates
    // (std::stable_sort may allocate a temporary buffer — not RT-safe).
    for (uint32_t i = 1; i < count; ++i) {
        VKEvent key = events[i];
        uint32_t j = i;
        while (j > 0 && events[j - 1].sampleOffset > key.sampleOffset) { events[j] = events[j - 1]; --j; }
        events[j] = key;
    }

    k->render(channels, channelCount, frameCount, events, count);

    // MIDI out (Phase 7): hand this buffer's events to the host's output block.
    if (const void* raw = k->midiOutBlock()) {
        AUMIDIOutputEventBlock block = (__bridge AUMIDIOutputEventBlock)raw;
        const VKMidiOutEvent* out = nullptr;
        const uint32_t n = k->midiOutput(&out);
        for (uint32_t i = 0; i < n; ++i)
            block(static_cast<AUEventSampleTime>(timestampSampleTime) + out[i].sampleOffset, 0, out[i].length, out[i].data);
    }
    return noErr;
}

// The kernel keeps a retained reference to the AU's MIDI output block.
extern "C" void vk_au_set_midi_output_block(VKKernel* handle, const void* block) {
    auto* k = reinterpret_cast<Kernel*>(handle);
    if (!k) return;
    const void* old = k->midiOutBlock();
    k->setMidiOutBlock(block ? CFBridgingRetain((__bridge id)block) : nullptr);
    if (old) CFRelease(old);
}

#endif // __APPLE__
