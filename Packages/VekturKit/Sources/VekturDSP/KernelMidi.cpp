// KernelMidi.cpp — Phase 7 MIDI engine of the kernel: MIDI 1.0 and MIDI 2.0
// (UMP) input, RPN/NRPN, 14-bit CC, program change, MIDI clock input,
// MIDI Learn, MIDI output (notes, clock). Everything here runs on the render
// thread, is allocation-free and bounded.
#include "Kernel.hpp"

#include <algorithm>
#include <cmath>

namespace vektur {

namespace {
constexpr uint64_t kMpeModeP = VK_P_GLOBAL_MPE_MODE, kMpeMembersP = VK_P_GLOBAL_MPE_MEMBER_CHANNELS, kMpeRangeP = VK_P_GLOBAL_MPE_PITCH_RANGE;
inline bool reservedCC(uint8_t cc) noexcept {
    return cc == 0 || cc == 32 || cc == 6 || cc == 38 || (cc >= 96 && cc <= 101) || cc >= 120;
}
constexpr double kTwo32 = 4294967295.0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
float Kernel::velocityCurve(float v) const noexcept {
    v = std::clamp(v, 0.f, 1.f);
    switch (std::lround(params_.get(VK_P_GLOBAL_VELOCITY_CURVE))) {
        case 1: return std::pow(v, 0.6f);     // soft: more level from a light touch
        case 2: return std::pow(v, 1.6f);     // hard
        case 3: return 100.f / 127.f;         // fixed
        default: return v;
    }
}

bool Kernel::channelAccepted(int ch) const noexcept {
    const long rc = std::lround(params_.get(VK_P_GLOBAL_RECEIVE_CHANNEL));
    if (rc == 0) return true;
    if (std::lround(params_.get(kMpeModeP)) != 0) return true;   // MPE uses its zone's channels
    return ch == rc - 1;
}

void Kernel::setParamFromMidi(uint64_t address, float value) noexcept {
    if (address >= VK_ADDRESS_SPACE) return;
    params_.set(address, value);
    midiDirty_[address >> 5].fetch_or(1u << (address & 31), std::memory_order_relaxed);
    midiDirtyPending_ = true;
}

uint32_t Kernel::takeMidiChanges(uint64_t* out, uint32_t cap) noexcept {
    uint32_t n = 0;
    for (uint32_t w = 0; w < midiDirty_.size(); ++w) {
        uint32_t bits = midiDirty_[w].exchange(0, std::memory_order_relaxed);
        while (bits) {
            const int b = __builtin_ctz(bits);
            bits &= bits - 1;
            if (out && n < cap) out[n] = static_cast<uint64_t>(w) * 32u + static_cast<uint64_t>(b);
            ++n;
        }
    }
    return std::min(n, cap);
}

void Kernel::learned(uint8_t kind, int ch, uint16_t number) noexcept {
    if (!learnArmed_.load(std::memory_order_relaxed)) return;
    learnArmed_.store(false, std::memory_order_relaxed);
    midiUi_.push(VKMidiUIEvent{VK_MIDI_UI_LEARNED, kind, static_cast<uint8_t>(ch), 0, 0, number});
}

void Kernel::applyMappings(uint8_t kind, int ch, uint16_t number, float in01, uint8_t raw7, bool gate, int relStep) noexcept {
    const midi::MappingTable& t = mappings_.front();
    for (uint32_t i = 0; i < t.count; ++i) {
        const VKMidiMapping& m = t.m[i];
        if (m.source != kind || m.number != number) continue;
        if (m.channel != VK_MIDI_ANY_CHANNEL && m.channel != ch) continue;
        uint8_t r = raw7;
        if (relStep != 0) {   // NRPN increment / decrement: encode ±1 in the mapping's own encoding
            if (m.relative == VK_MIDI_REL_BINARY_OFFSET) r = relStep > 0 ? 65 : 63;
            else if (m.relative == VK_MIDI_REL_SIGNED_BIT) r = relStep > 0 ? 1 : 65;
            else r = relStep > 0 ? 1 : 127;
            if (m.mode != VK_MIDI_MODE_RELATIVE) continue;
        }
        float v;
        if (midi::apply(m, mapState_[i], in01, r, gate, params_, v)) setParamFromMidi(m.address, v);
    }
}

void Kernel::midiStatus(VKMidiStatus& s) const noexcept {
    s.clockTempo = stClockTempo_.load(std::memory_order_relaxed);
    const uint32_t f = stClockFlags_.load(std::memory_order_relaxed);
    s.clockRunning = f & 1u; s.clockActive = f & 2u; s.syncSource = static_cast<uint8_t>((f >> 4) & 3u);
    s.messagesIn = midiIn_.load(std::memory_order_relaxed);
    s.messagesOut = midiOutTotal_.load(std::memory_order_relaxed);
    const uint32_t last = midiLast_.load(std::memory_order_relaxed);
    s.lastStatus = static_cast<uint8_t>(last >> 16); s.lastData1 = static_cast<uint8_t>(last >> 8); s.lastData2 = static_cast<uint8_t>(last);
}

// ---------------------------------------------------------------------------
// MIDI clock in
// ---------------------------------------------------------------------------
bool Kernel::clockActive() const noexcept {
    return clk_.lastTick >= 0.0 && static_cast<double>(sampleTime_) - clk_.lastTick < 0.5 * sampleRate_ && clk_.interval > 0.0;
}
double Kernel::clockTempo() const noexcept { return clk_.interval > 0.0 ? std::clamp(60.0 * sampleRate_ / (clk_.interval * 24.0), 20.0, 400.0) : 120.0; }
double Kernel::clockBeatNow() const noexcept {
    if (!clk_.running || clk_.interval <= 0.0) return clk_.tickBeat;
    const double since = (static_cast<double>(sampleTime_) - clk_.lastTick) / clk_.interval;
    return clk_.tickBeat + std::clamp(since, 0.0, 1.0) / 24.0;   // never run past the next tick
}

void Kernel::handleSystem(uint8_t status, uint8_t d1, uint8_t d2) noexcept {
    const double t = static_cast<double>(eventSample_);
    switch (status) {
        case 0xF8: {   // timing clock, 24 per quarter note
            if (clk_.lastTick >= 0.0) {
                const double iv = t - clk_.lastTick;
                if (iv > 0.0 && iv < sampleRate_) {
                    const double a = clk_.ticks < 24 ? 0.5 : 0.08;   // converge quickly, then smooth jitter
                    clk_.interval = clk_.interval > 0.0 ? clk_.interval + a * (iv - clk_.interval) : iv;
                }
            }
            clk_.lastTick = t;
            ++clk_.ticks;
            if (clk_.running) clk_.tickBeat += 1.0 / 24.0;
            break;
        }
        case 0xFA: clk_.running = true; clk_.tickBeat = -1.0 / 24.0; clk_.sppBeat = 0.0; break;           // start: next tick is beat 0
        case 0xFB: clk_.running = true; clk_.tickBeat = clk_.sppBeat - 1.0 / 24.0; break;                 // continue
        case 0xFC: clk_.running = false; break;                                                           // stop
        case 0xF2: clk_.sppBeat = static_cast<double>(d1 | (d2 << 7)) / 4.0; if (!clk_.running) clk_.tickBeat = clk_.sppBeat; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// MIDI 1.0
// ---------------------------------------------------------------------------
void Kernel::handleMidi(const uint8_t* d, uint8_t len) noexcept {
    if (len == 0) return;
    midiIn_.fetch_add(1, std::memory_order_relaxed);
    if (d[0] >= 0xF0) { handleSystem(d[0], len > 1 ? d[1] & 0x7F : 0, len > 2 ? d[2] & 0x7F : 0); return; }
    const uint8_t status = d[0] & 0xF0;
    const int ch = d[0] & 0x0F;
    const uint8_t d1 = len > 1 ? (d[1] & 0x7F) : 0;
    const uint8_t d2 = len > 2 ? (d[2] & 0x7F) : 0;
    midiLast_.store(static_cast<uint32_t>(d[0]) << 16 | static_cast<uint32_t>(d1) << 8 | d2, std::memory_order_relaxed);
    if (!channelAccepted(ch)) return;

    // MPE: member channels carry per-note data; the master channel (and any
    // other channel) keeps channel-wide behaviour.
    const int mpe = static_cast<int>(std::lround(params_.get(kMpeModeP)));
    bool member = false;
    if (mpe != 0) {
        const int members = 1 + static_cast<int>(std::clamp(std::lround(params_.get(kMpeMembersP)), 0L, 14L));
        member = mpe == 1 ? (ch >= 1 && ch <= members) : (ch <= 14 && ch >= 15 - members);
    }

    // Learn & mappings (sources other than notes are also handled normally
    // below). Per-note expression on MPE member channels (bend, pressure,
    // timbre CC 74) is never mapped to global parameters.
    switch (status) {
        case 0xB0:
            if (!reservedCC(d1) && !(member && d1 == 74)) {
                if (d1 != 64) learned(VK_MIDI_SRC_CC, ch, d1);   // a sustain pedal held while learning is not what was meant
                applyMappings(VK_MIDI_SRC_CC, ch, d1, d2 / 127.f, d2, d2 >= 64);
            }
            break;
        case 0xE0:
            if (!member) {
                const int v = (d2 << 7) | d1;
                learned(VK_MIDI_SRC_PITCH_BEND, ch, 0);
                applyMappings(VK_MIDI_SRC_PITCH_BEND, ch, 0, static_cast<float>(v) / 16383.f, d2, v >= 8192);
            }
            break;
        case 0xD0:
            if (!member) { learned(VK_MIDI_SRC_CHANNEL_PRESSURE, ch, 0); applyMappings(VK_MIDI_SRC_CHANNEL_PRESSURE, ch, 0, d1 / 127.f, d1, d1 >= 64); }
            break;
        case 0x90:
            if (d2 > 0) { learned(VK_MIDI_SRC_NOTE, ch, d1); applyMappings(VK_MIDI_SRC_NOTE, ch, d1, 1.f, d2, true); }
            else applyMappings(VK_MIDI_SRC_NOTE, ch, d1, 0.f, 0, false);
            break;
        case 0x80: applyMappings(VK_MIDI_SRC_NOTE, ch, d1, 0.f, 0, false); break;
        default: break;
    }

    if (member && handleMpeMember(status, ch, d1, d2)) return;
    switch (status) {
        case 0x90:
            if (d2 == 0) noteOff(d1);
            else { const float v = velocityCurve(d2 / 127.f); noteOn(d1, std::max(1, static_cast<int>(std::lround(v * 127.f)))); }
            break;
        case 0x80: noteOff(d1); break;
        case 0xE0: pitchBendNorm_ = std::clamp(static_cast<float>(static_cast<int>((d2 << 7) | d1) - 8192) / 8192.f, -1.f, 1.f); break;
        case 0xB0: handleControl(ch, d1, d2, d2 / 127.f); break;
        case 0xD0: ctl_[MS_AFTERTOUCH] = d1 / 127.f; break;   // channel pressure
        case 0xA0:                                             // polyphonic key pressure
            for (auto& v : voices_) if (!v.isIdle() && (v.key() == d1 || v.note() == d1)) v.setPolyPressure(d2 / 127.f);
            break;
        case 0xC0:
            if (params_.get(VK_P_GLOBAL_PROGRAM_CHANGE) >= 0.5f)
                midiUi_.push(VKMidiUIEvent{VK_MIDI_UI_PROGRAM, 0, static_cast<uint8_t>(ch), chan_[static_cast<size_t>(ch)].bankMsb, chan_[static_cast<size_t>(ch)].bankLsb, d1});
            break;
        default: break;
    }
}

// Channel-wide controllers: built-ins, bank select, RPN/NRPN, 14-bit pairs.
void Kernel::handleControl(int ch, uint8_t cc, uint8_t v7, float hiRes) noexcept {
    ChannelState& cs = chan_[static_cast<size_t>(ch)];
    switch (cc) {
        case 0: cs.bankMsb = v7; return;
        case 32: cs.bankLsb = v7; return;
        case 1: ctl_[MS_MODWHEEL] = hiRes; return;
        case 2: ctl_[MS_BREATH] = hiRes; return;
        case 11: ctl_[MS_EXPRESSION] = hiRes; return;
        case 64: setSustainPedal(v7 >= 64); return;
        case 120: panic(); return;          // All Sound Off
        case 123: allNotesOff(); return;    // All Notes Off
        case 101: cs.rpnMsb = v7; cs.nrpn = false; return;
        case 100: cs.rpnLsb = v7; cs.nrpn = false; return;
        case 99: cs.nrpnMsb = v7; cs.nrpn = true; return;
        case 98: cs.nrpnLsb = v7; cs.nrpn = true; return;
        case 6: case 38: {
            if (cc == 6) { cs.dataMsb = v7; cs.dataLsb = 0; } else cs.dataLsb = v7;
            const uint16_t data = static_cast<uint16_t>(cs.dataMsb << 7 | cs.dataLsb);
            if (cs.nrpn) {
                const auto number = static_cast<uint16_t>(cs.nrpnMsb << 7 | cs.nrpnLsb);
                learned(VK_MIDI_SRC_NRPN, ch, number);
                applyMappings(VK_MIDI_SRC_NRPN, ch, number, data / 16383.f, cs.dataMsb, data >= 8192);
            } else if (cs.rpnMsb != 127 || cs.rpnLsb != 127) handleRpn(ch, static_cast<uint16_t>(cs.rpnMsb << 7 | cs.rpnLsb), data);
            return;
        }
        case 96: case 97:
            if (cs.nrpn) applyMappings(VK_MIDI_SRC_NRPN, ch, static_cast<uint16_t>(cs.nrpnMsb << 7 | cs.nrpnLsb), 0.f, 0, false, cc == 96 ? 1 : -1);
            return;
        default: break;
    }
    // 14-bit controller pairs (MSB 0…31, LSB 32…63).
    if (cc < 32) {
        cs.ccMsb[cc] = v7;
        applyMappings(VK_MIDI_SRC_CC14, ch, cc, static_cast<float>(v7 << 7) / 16383.f, v7, v7 >= 64);
    } else if (cc < 64) {
        const uint8_t msb = cs.ccMsb[cc - 32];
        applyMappings(VK_MIDI_SRC_CC14, ch, static_cast<uint16_t>(cc - 32), static_cast<float>(msb << 7 | v7) / 16383.f, msb, msb >= 64);
    }
}

// Registered parameters: 0 bend range, 1 fine tune, 6 MPE configuration (MCM).
void Kernel::handleRpn(int ch, uint16_t number, uint16_t data14) noexcept {
    const uint8_t msb = static_cast<uint8_t>(data14 >> 7), lsb = static_cast<uint8_t>(data14 & 0x7F);
    switch (number) {
        case 0: {   // pitch bend sensitivity (semitones + cents)
            const float semis = static_cast<float>(msb) + static_cast<float>(lsb) / 100.f;
            const int mpe = static_cast<int>(std::lround(params_.get(kMpeModeP)));
            const bool master = (mpe == 1 && ch == 0) || (mpe == 2 && ch == 15);
            if (mpe != 0 && !master) setParamFromMidi(kMpeRangeP, semis);
            else setParamFromMidi(VK_P_GLOBAL_PITCH_BEND_RANGE, semis);
            break;
        }
        case 1:     // master fine tuning: 8192 = 0, ±100 cents
            setParamFromMidi(VK_P_GLOBAL_MASTER_TUNE, (static_cast<float>(data14) - 8192.f) / 8192.f * 100.f);
            break;
        case 6: {   // MPE Configuration Message on a zone's master channel
            if (ch != 0 && ch != 15) break;
            if (msb == 0) {   // zone off
                const long cur = std::lround(params_.get(kMpeModeP));
                if ((ch == 0 && cur == 1) || (ch == 15 && cur == 2)) setParamFromMidi(kMpeModeP, 0.f);
            } else {
                setParamFromMidi(kMpeModeP, ch == 0 ? 1.f : 2.f);
                setParamFromMidi(kMpeMembersP, static_cast<float>(std::min<int>(msb, 15) - 1));
            }
            break;
        }
        default: break;
    }
}

// MPE member channel: per-note pitch bend, pressure and timbre (CC 74).
// Returns false for messages that should keep channel-wide handling.
bool Kernel::handleMpeMember(uint8_t status, int ch, uint8_t d1, uint8_t d2) noexcept {
    switch (status) {
        case 0x90:
            if (d2 == 0) noteOff(d1, ch);
            else { const float v = velocityCurve(d2 / 127.f); noteOn(d1, std::max(1, static_cast<int>(std::lround(v * 127.f))), ch); }
            return true;
        case 0x80: noteOff(d1, ch); return true;
        case 0xE0: {
            chanBend_[ch] = std::clamp(static_cast<float>(static_cast<int>((d2 << 7) | d1) - 8192) / 8192.f, -1.f, 1.f);
            const float semis = chanBend_[ch] * params_.get(kMpeRangeP);
            for (auto& v : voices_) if (!v.isIdle() && v.channel() == ch) v.setNoteBend(semis);
            return true;
        }
        case 0xD0:
            chanPressure_[ch] = d1 / 127.f;
            for (auto& v : voices_) if (!v.isIdle() && v.channel() == ch) v.setPolyPressure(chanPressure_[ch]);
            return true;
        case 0xB0:
            if (d1 == 74) {
                chanTimbre_[ch] = d2 / 127.f;
                for (auto& v : voices_) if (!v.isIdle() && v.channel() == ch) v.setTimbre(chanTimbre_[ch]);
                return true;
            }
            // RPN on a member channel (per-note bend range) is channel-wide data handling.
            return !(d1 == 64 || d1 == 120 || d1 == 123 || d1 == 6 || d1 == 38 || (d1 >= 98 && d1 <= 101));
        default: return true;   // poly pressure etc. are not used on MPE member channels
    }
}

// ---------------------------------------------------------------------------
// MIDI 2.0 (UMP)
// ---------------------------------------------------------------------------
void Kernel::handleUmp(uint32_t w0, uint32_t w1) noexcept {
    const uint32_t type = w0 >> 28;
    if (type == 0x1 || type == 0x2) {   // system / MIDI 1.0 channel voice in UMP
        const uint8_t b[3] = {static_cast<uint8_t>(w0 >> 16), static_cast<uint8_t>((w0 >> 8) & 0x7F), static_cast<uint8_t>(w0 & 0x7F)};
        const uint8_t s = b[0] & 0xF0;
        handleMidi(b, (type == 0x2 && (s == 0xC0 || s == 0xD0)) ? 2 : 3);
        return;
    }
    if (type != 0x4) return;
    midiIn_.fetch_add(1, std::memory_order_relaxed);
    const uint8_t op = static_cast<uint8_t>((w0 >> 20) & 0xF);
    const int ch = static_cast<int>((w0 >> 16) & 0xF);
    const uint8_t idx = static_cast<uint8_t>((w0 >> 8) & 0x7F), low = static_cast<uint8_t>(w0 & 0xFF);
    midiLast_.store(static_cast<uint32_t>((op << 4) | ch) << 16 | static_cast<uint32_t>(idx) << 8 | (w1 >> 25), std::memory_order_relaxed);
    const int mpe = static_cast<int>(std::lround(params_.get(kMpeModeP)));
    // MPE zones keep their MIDI 1.0 semantics: down-convert (high-resolution velocity kept).
    if (mpe != 0 && (op == 0x9 || op == 0x8 || op == 0xE || op == 0xD || op == 0xB)) {
        uint8_t b[3] = {static_cast<uint8_t>((op << 4) | ch), idx, static_cast<uint8_t>(w1 >> 25)};
        if (op == 0x9) { b[2] = static_cast<uint8_t>(std::max<uint32_t>(1, w1 >> 25)); hiResVel_ = velocityCurve(static_cast<float>(w1 >> 16) / 65535.f); }
        if (op == 0xE) { const uint32_t v14 = w1 >> 18; b[1] = static_cast<uint8_t>(v14 & 0x7F); b[2] = static_cast<uint8_t>(v14 >> 7); }
        if (op == 0xD) b[1] = static_cast<uint8_t>(w1 >> 25);
        handleMidi(b, op == 0xD ? 2 : 3);
        midiIn_.fetch_sub(1, std::memory_order_relaxed);
        hiResVel_ = -1.f;
        return;
    }
    if (!channelAccepted(ch)) return;
    const float v32 = static_cast<float>(static_cast<double>(w1) / kTwo32);
    switch (op) {
        case 0x9: {   // note on, 16-bit velocity (0 is a valid note-on in MIDI 2.0)
            const float vel = velocityCurve(std::max(1.f, static_cast<float>(w1 >> 16)) / 65535.f);
            learned(VK_MIDI_SRC_NOTE, ch, idx); applyMappings(VK_MIDI_SRC_NOTE, ch, idx, 1.f, 127, true);
            hiResVel_ = vel;
            noteOn(idx, std::max(1, static_cast<int>(std::lround(vel * 127.f))));
            hiResVel_ = -1.f;
            break;
        }
        case 0x8: applyMappings(VK_MIDI_SRC_NOTE, ch, idx, 0.f, 0, false); noteOff(idx); break;
        case 0xA: for (auto& v : voices_) if (!v.isIdle() && v.key() == idx) v.setPolyPressure(v32); break;
        case 0xB:
            if (!reservedCC(idx)) { if (idx != 64) learned(VK_MIDI_SRC_CC, ch, idx); applyMappings(VK_MIDI_SRC_CC, ch, idx, v32, static_cast<uint8_t>(w1 >> 25), v32 >= 0.5f); }
            handleControl(ch, idx, static_cast<uint8_t>(w1 >> 25), v32);
            break;
        case 0xD: ctl_[MS_AFTERTOUCH] = v32; learned(VK_MIDI_SRC_CHANNEL_PRESSURE, ch, 0); applyMappings(VK_MIDI_SRC_CHANNEL_PRESSURE, ch, 0, v32, static_cast<uint8_t>(w1 >> 25), v32 >= 0.5f); break;
        case 0xE:
            pitchBendNorm_ = std::clamp(static_cast<float>((static_cast<double>(w1) - 2147483648.0) / 2147483648.0), -1.f, 1.f);
            learned(VK_MIDI_SRC_PITCH_BEND, ch, 0); applyMappings(VK_MIDI_SRC_PITCH_BEND, ch, 0, v32, static_cast<uint8_t>(w1 >> 25), v32 >= 0.5f);
            break;
        case 0xC:
            if (low & 1) { chan_[static_cast<size_t>(ch)].bankMsb = static_cast<uint8_t>((w1 >> 8) & 0x7F); chan_[static_cast<size_t>(ch)].bankLsb = static_cast<uint8_t>(w1 & 0x7F); }
            if (params_.get(VK_P_GLOBAL_PROGRAM_CHANGE) >= 0.5f)
                midiUi_.push(VKMidiUIEvent{VK_MIDI_UI_PROGRAM, 0, static_cast<uint8_t>(ch), chan_[static_cast<size_t>(ch)].bankMsb, chan_[static_cast<size_t>(ch)].bankLsb,
                                           static_cast<uint16_t>((w1 >> 24) & 0x7F)});
            break;
        case 0x2:     // registered controller (RPN), 32-bit data
            handleRpn(ch, static_cast<uint16_t>(idx << 7 | (low & 0x7F)), static_cast<uint16_t>(w1 >> 18));
            break;
        case 0x3: {   // assignable controller (NRPN), 32-bit data
            const auto number = static_cast<uint16_t>(idx << 7 | (low & 0x7F));
            learned(VK_MIDI_SRC_NRPN, ch, number);
            applyMappings(VK_MIDI_SRC_NRPN, ch, number, v32, static_cast<uint8_t>(w1 >> 25), v32 >= 0.5f);
            break;
        }
        case 0x6: {   // per-note pitch bend (range: the per-note bend range parameter)
            const float semis = static_cast<float>((static_cast<double>(w1) - 2147483648.0) / 2147483648.0) * params_.get(kMpeRangeP);
            for (auto& v : voices_) if (!v.isIdle() && v.key() == idx) v.setNoteBend(semis);
            break;
        }
        case 0x0: case 0x1:   // registered / assignable per-note controller: #74 = per-note timbre
            if (low == 74) for (auto& v : voices_) if (!v.isIdle() && v.key() == idx) v.setTimbre(v32);
            break;
        case 0xF:     // per-note management: reset (bit 0) returns the note's controllers to default
            if (low & 1) for (auto& v : voices_) if (!v.isIdle() && v.key() == idx) { v.setNoteBend(0.f); v.setTimbre(0.5f); }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// MIDI out
// ---------------------------------------------------------------------------
void Kernel::midiOutPush(uint32_t offset, uint8_t a, uint8_t b, uint8_t c, uint8_t len) noexcept {
    if (midiOutCount_ >= VK_MIDI_OUT_MAX) { pendingFlags_ |= VK_TELEMETRY_FLAG_EVENTS_DROP; return; }
    VKMidiOutEvent& e = midiOut_[midiOutCount_++];
    e.sampleOffset = offset; e.length = len; e.data[0] = a; e.data[1] = b; e.data[2] = c;
    midiOutTotal_.fetch_add(1, std::memory_order_relaxed);
}

void Kernel::midiOutNote(int note, int velocity) noexcept {
    const auto ch = static_cast<uint8_t>(std::clamp(std::lround(params_.get(VK_P_GLOBAL_MIDI_OUT_CHANNEL)), 0L, 15L));
    if (velocity > 0) midiOutPush(spanPos_, static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), static_cast<uint8_t>(std::clamp(velocity, 1, 127)), 3);
    else midiOutPush(spanPos_, static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 64, 3);
}

// 24 PPQN clock on the generators' clock, plus start/stop when enabled/disabled.
void Kernel::clockOut(double beatStart, uint32_t offset, uint32_t frames) noexcept {
    const bool on = params_.get(VK_P_GLOBAL_MIDI_CLOCK_OUT) >= 0.5f;
    if (on != clockOutOn_) {
        clockOutOn_ = on;
        midiOutPush(offset, on ? 0xFA : 0xFC, 0, 0, 1);
        clockOutResync_ = true;
    }
    if (!on || frames == 0) return;
    const double beatsPerSample = tempo_ / 60.0 / sampleRate_;
    if (clockOutResync_) { nextClockOutTick_ = std::ceil(beatStart * 24.0 - 1e-9); clockOutResync_ = false; }
    const double beatEnd = beatStart + beatsPerSample * frames;
    while (nextClockOutTick_ / 24.0 < beatEnd - 1e-12) {
        const double at = (nextClockOutTick_ / 24.0 - beatStart) / beatsPerSample;
        midiOutPush(offset + static_cast<uint32_t>(std::clamp(std::ceil(at - 1e-9), 0.0, static_cast<double>(frames - 1))), 0xF8, 0, 0, 1);
        nextClockOutTick_ += 1.0;
    }
}

} // namespace vektur
