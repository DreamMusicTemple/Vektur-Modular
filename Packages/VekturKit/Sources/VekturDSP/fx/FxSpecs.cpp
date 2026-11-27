// FxSpecs.cpp — see FxSpecs.hpp.
#include "FxSpecs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vektur::fx {

namespace {
constexpr const char* kSync[] = {"Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
                                 "1/2 dotted", "1/4 dotted", "1/8 dotted", "1/2 triplet", "1/4 triplet", "1/8 triplet", "1/16 triplet"};
constexpr const char* kSlices[] = {"1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet"};
constexpr const char* kGrid[] = {"1/4", "1/2", "1 bar", "2 bars"};
constexpr const char* kHeads[] = {"1", "2", "3", "1+2", "2+3", "1+3", "1+2+3"};
constexpr const char* kIRs[] = {"Small Room", "Studio", "Chamber", "Concert Hall", "Cathedral", "Plate", "Spring Tank", "4×12 Cabinet", "User IR"};
constexpr const char* kSprings[] = {"1 spring", "2 springs", "3 springs"};
constexpr const char* kQMode[] = {"Constant Q", "Proportional Q"};
constexpr const char* kEnvShape[] = {"Hann", "Triangle", "Trapezoid"};
constexpr const char* kQuant[] = {"Off", "Octaves", "Fifths + Octaves"};
constexpr const char* kSpeed[] = {"7.5 ips", "15 ips", "30 ips"};
constexpr const char* kChannels[] = {"Clean", "Crunch", "Lead", "Fuzz"};
constexpr const char* kCabs[] = {"1×12 Open", "2×12 Open", "4×12 Closed", "1×8 Small"};
constexpr const char* kMics[] = {"Dynamic", "Condenser", "Ribbon"};
constexpr const char* kVoices[] = {"1 voice", "2 voices", "3 voices"};
constexpr const char* kStages[] = {"2", "4", "6", "8", "12"};
constexpr const char* kTremModes[] = {"Tremolo", "Auto-Pan", "Harmonic"};
constexpr const char* kShapes[] = {"Sine", "Triangle", "Square", "Saw"};
constexpr const char* kDetector[] = {"Peak", "RMS"};
constexpr const char* kCharacter[] = {"Clean", "Vintage"};

constexpr ParamSpec L(const char* n, Unit u, float lo, float hi, float d) { return {n, u, Curve::Linear, lo, hi, d, nullptr, 0, false}; }
constexpr ParamSpec G(const char* n, Unit u, float lo, float hi, float d) { return {n, u, Curve::Log, lo, hi, d, nullptr, 0, false}; }
template <size_t N>
constexpr ParamSpec C(const char* n, const char* const (&c)[N], float d, bool structural = false) {
    return {n, Unit::Choice, Curve::Choice, 0.f, static_cast<float>(N - 1), d, c, static_cast<uint8_t>(N), structural};
}
constexpr ParamSpec T(const char* n, float d) { return {n, Unit::Toggle, Curve::Toggle, 0.f, 1.f, d, nullptr, 0, false}; }
constexpr ParamSpec X{};

constexpr ParamSpec LowCut = G("Low Cut", Unit::Hz, 20.f, 2000.f, 20.f);
constexpr ParamSpec HighCut = G("High Cut", Unit::Hz, 500.f, 20000.f, 20000.f);
constexpr ParamSpec Width = L("Width", Unit::Percent, 0.f, 100.f, 100.f);
constexpr ParamSpec Ducking = L("Ducking", Unit::Percent, 0.f, 100.f, 0.f);

const TypeSpec kSpecs[FxTypeCount] = {
    {"Empty", "", 100.f, {X, X, X, X, X, X, X, X, X, X, X, X}},
    {"Stereo Delay", "Delay", 30.f, {C("Sync L", kSync, 11), C("Sync R", kSync, 5), G("Time L", Unit::Ms, 1.f, 2500.f, 375.f), G("Time R", Unit::Ms, 1.f, 2500.f, 500.f),
        L("Feedback", Unit::Percent, 0.f, 98.f, 35.f), L("Cross-feed", Unit::Percent, 0.f, 100.f, 0.f), LowCut, HighCut,
        L("Mod Depth", Unit::Percent, 0.f, 100.f, 0.f), G("Mod Rate", Unit::Hz, 0.05f, 5.f, 0.4f), L("Drive", Unit::Percent, 0.f, 100.f, 0.f), Ducking}},
    {"Ping-Pong Delay", "Delay", 30.f, {C("Sync", kSync, 6), G("Time", Unit::Ms, 1.f, 2500.f, 375.f), L("Feedback", Unit::Percent, 0.f, 98.f, 45.f),
        L("Spread", Unit::Percent, 0.f, 100.f, 100.f), LowCut, HighCut, L("Mod Depth", Unit::Percent, 0.f, 100.f, 0.f), G("Mod Rate", Unit::Hz, 0.05f, 5.f, 0.4f),
        L("Drive", Unit::Percent, 0.f, 100.f, 0.f), Ducking, L("Input Pan", Unit::Percent, -100.f, 100.f, -100.f), Width}},
    {"Tape Echo", "Delay", 35.f, {C("Sync", kSync, 0), G("Repeat Rate", Unit::Ms, 50.f, 1500.f, 330.f), L("Intensity", Unit::Percent, 0.f, 105.f, 45.f),
        C("Heads", kHeads, 0), L("Wow", Unit::Percent, 0.f, 100.f, 25.f), L("Flutter", Unit::Percent, 0.f, 100.f, 20.f), L("Saturation", Unit::Percent, 0.f, 100.f, 35.f),
        L("Tone", Unit::Percent, 0.f, 100.f, 55.f), L("Tape Age", Unit::Percent, 0.f, 100.f, 20.f), G("Low Cut", Unit::Hz, 20.f, 1000.f, 80.f), Width, Ducking}},
    {"BBD Delay", "Delay", 35.f, {C("Sync", kSync, 0), G("Time", Unit::Ms, 20.f, 600.f, 280.f), L("Feedback", Unit::Percent, 0.f, 100.f, 40.f),
        L("Tone", Unit::Percent, 0.f, 100.f, 50.f), L("Mod Depth", Unit::Percent, 0.f, 100.f, 20.f), G("Mod Rate", Unit::Hz, 0.05f, 8.f, 0.6f),
        L("Grit", Unit::Percent, 0.f, 100.f, 25.f), L("Drive", Unit::Percent, 0.f, 100.f, 20.f), Width, G("Low Cut", Unit::Hz, 20.f, 1000.f, 40.f), X, X}},
    {"Plate Reverb", "Reverb", 25.f, {G("Pre-delay", Unit::Ms, 0.1f, 250.f, 10.f), L("Decay", Unit::Percent, 0.f, 100.f, 55.f), L("Size", Unit::Percent, 10.f, 100.f, 80.f),
        L("Damping", Unit::Percent, 0.f, 100.f, 35.f), L("Bandwidth", Unit::Percent, 0.f, 100.f, 80.f), L("Diffusion", Unit::Percent, 0.f, 100.f, 75.f),
        L("Modulation", Unit::Percent, 0.f, 100.f, 30.f), LowCut, HighCut, Width, X, X}},
    {"Hall Reverb", "Reverb", 25.f, {G("Pre-delay", Unit::Ms, 0.1f, 250.f, 20.f), G("Decay", Unit::Seconds, 0.3f, 20.f, 2.8f), L("Size", Unit::Percent, 10.f, 100.f, 75.f),
        G("High Decay", Unit::Multiplier, 0.1f, 1.f, 0.5f), G("Low Decay", Unit::Multiplier, 0.5f, 2.f, 1.1f), L("Diffusion", Unit::Percent, 0.f, 100.f, 80.f),
        L("Mod Depth", Unit::Percent, 0.f, 100.f, 30.f), G("Mod Rate", Unit::Hz, 0.05f, 3.f, 0.5f), L("Early Level", Unit::Percent, 0.f, 100.f, 40.f), LowCut, HighCut, Width}},
    {"Room Reverb", "Reverb", 20.f, {G("Pre-delay", Unit::Ms, 0.1f, 100.f, 2.f), G("Decay", Unit::Seconds, 0.1f, 4.f, 0.7f), L("Size", Unit::Percent, 10.f, 100.f, 45.f),
        L("Damping", Unit::Percent, 0.f, 100.f, 45.f), L("Early Level", Unit::Percent, 0.f, 100.f, 70.f), L("Diffusion", Unit::Percent, 0.f, 100.f, 70.f),
        LowCut, HighCut, Width, X, X, X}},
    {"Spring Reverb", "Reverb", 30.f, {G("Decay", Unit::Seconds, 0.5f, 6.f, 2.0f), L("Tension", Unit::Percent, 0.f, 100.f, 50.f), C("Springs", kSprings, 1),
        L("Drip", Unit::Percent, 0.f, 100.f, 35.f), L("Tone", Unit::Percent, 0.f, 100.f, 50.f), G("Low Cut", Unit::Hz, 20.f, 1000.f, 120.f), Width, X, X, X, X, X}},
    {"Convolution Reverb", "Reverb", 25.f, {C("Impulse", kIRs, 3, true), G("Pre-delay", Unit::Ms, 0.1f, 250.f, 0.1f), LowCut, HighCut, Width,
        L("Gain", Unit::Db, -24.f, 12.f, 0.f), X, X, X, X, X, X}},
    {"Parametric EQ", "EQ", 100.f, {G("Low Cut", Unit::Hz, 10.f, 1000.f, 10.f), G("Low Shelf Freq", Unit::Hz, 30.f, 1000.f, 100.f), L("Low Shelf Gain", Unit::Db, -18.f, 18.f, 0.f),
        G("Peak 1 Freq", Unit::Hz, 50.f, 8000.f, 400.f), L("Peak 1 Gain", Unit::Db, -18.f, 18.f, 0.f), G("Peak 1 Q", Unit::Ratio, 0.2f, 12.f, 1.f),
        G("Peak 2 Freq", Unit::Hz, 200.f, 16000.f, 2500.f), L("Peak 2 Gain", Unit::Db, -18.f, 18.f, 0.f), G("Peak 2 Q", Unit::Ratio, 0.2f, 12.f, 1.f),
        G("High Shelf Freq", Unit::Hz, 1000.f, 18000.f, 8000.f), L("High Shelf Gain", Unit::Db, -18.f, 18.f, 0.f), G("High Cut", Unit::Hz, 1000.f, 22000.f, 22000.f)}},
    {"Graphic EQ", "EQ", 100.f, {L("31 Hz", Unit::Db, -12.f, 12.f, 0.f), L("63 Hz", Unit::Db, -12.f, 12.f, 0.f), L("125 Hz", Unit::Db, -12.f, 12.f, 0.f),
        L("250 Hz", Unit::Db, -12.f, 12.f, 0.f), L("500 Hz", Unit::Db, -12.f, 12.f, 0.f), L("1 kHz", Unit::Db, -12.f, 12.f, 0.f), L("2 kHz", Unit::Db, -12.f, 12.f, 0.f),
        L("4 kHz", Unit::Db, -12.f, 12.f, 0.f), L("8 kHz", Unit::Db, -12.f, 12.f, 0.f), L("16 kHz", Unit::Db, -12.f, 12.f, 0.f),
        G("Q", Unit::Ratio, 0.5f, 4.f, 1.4f), C("Q Mode", kQMode, 1)}},
    {"Spectral", "Spectral", 100.f, {T("Freeze", 0.f), L("Blur", Unit::Percent, 0.f, 100.f, 0.f), L("Shift", Unit::Semitones, -24.f, 24.f, 0.f),
        L("Smear", Unit::Percent, 0.f, 100.f, 0.f), L("Tilt", Unit::Db, -6.f, 6.f, 0.f), L("Threshold", Unit::Db, -100.f, 0.f, -100.f),
        L("Phase Scatter", Unit::Percent, 0.f, 100.f, 0.f), X, X, X, X, X}},
    {"Granular", "Granular", 50.f, {G("Grain Size", Unit::Ms, 10.f, 500.f, 80.f), G("Density", Unit::PerSecond, 1.f, 100.f, 20.f), L("Pitch", Unit::Semitones, -24.f, 24.f, 0.f),
        L("Pitch Spread", Unit::Semitones, 0.f, 12.f, 0.f), L("Position", Unit::Ms, 0.f, 2000.f, 100.f), L("Spray", Unit::Ms, 0.f, 1000.f, 50.f),
        L("Reverse", Unit::Percent, 0.f, 100.f, 0.f), T("Freeze", 0.f), L("Feedback", Unit::Percent, 0.f, 90.f, 0.f), L("Stereo Spread", Unit::Percent, 0.f, 100.f, 60.f),
        C("Envelope", kEnvShape, 0), C("Pitch Quantise", kQuant, 0)}},
    {"Tape", "Tape", 100.f, {L("Drive", Unit::Db, 0.f, 24.f, 6.f), L("Bias", Unit::Percent, 0.f, 100.f, 30.f), C("Speed", kSpeed, 1), L("Wow", Unit::Percent, 0.f, 100.f, 10.f),
        L("Flutter", Unit::Percent, 0.f, 100.f, 10.f), L("Hiss", Unit::Percent, 0.f, 100.f, 0.f), L("Head Bump", Unit::Percent, 0.f, 100.f, 40.f),
        L("HF Loss", Unit::Percent, 0.f, 100.f, 30.f), T("Auto Gain", 1.f), X, X, X}},
    {"Stutter", "Glitch", 100.f, {C("Slice", kSlices, 2), L("Chance", Unit::Percent, 0.f, 100.f, 35.f), C("Grid", kGrid, 1), L("Length", Unit::Percent, 10.f, 100.f, 100.f),
        L("Reverse", Unit::Percent, 0.f, 100.f, 0.f), L("Pitch Drop", Unit::Semitones, 0.f, 12.f, 0.f), L("Repeat Decay", Unit::Percent, 0.f, 100.f, 0.f),
        L("Tape Stop", Unit::Percent, 0.f, 100.f, 0.f), L("Seed", Unit::None, 0.f, 999.f, 1.f), X, X, X}},
    {"Lo-Fi", "Glitch", 100.f, {L("Bits", Unit::Bits, 1.f, 16.f, 10.f), G("Sample Rate", Unit::Hz, 200.f, 48000.f, 12000.f), L("Jitter", Unit::Percent, 0.f, 100.f, 0.f),
        L("Noise", Unit::Percent, 0.f, 100.f, 0.f), T("Anti-alias", 1.f), L("Drive", Unit::Percent, 0.f, 100.f, 0.f), X, X, X, X, X, X}},
    {"Amp", "Amp & Cab", 100.f, {C("Channel", kChannels, 1), L("Gain", Unit::Percent, 0.f, 100.f, 50.f), L("Bass", Unit::Db, -12.f, 12.f, 0.f), L("Mid", Unit::Db, -12.f, 12.f, 0.f),
        L("Treble", Unit::Db, -12.f, 12.f, 0.f), L("Presence", Unit::Db, -12.f, 12.f, 0.f), L("Master", Unit::Db, -36.f, 6.f, -12.f), L("Sag", Unit::Percent, 0.f, 100.f, 20.f),
        T("Bright", 0.f), X, X, X}},
    {"Cabinet", "Amp & Cab", 100.f, {C("Cabinet", kCabs, 2), C("Microphone", kMics, 0), L("Mic Position", Unit::Percent, 0.f, 100.f, 30.f), L("Distance", Unit::Percent, 0.f, 100.f, 20.f),
        G("Low Cut", Unit::Hz, 20.f, 400.f, 60.f), G("High Cut", Unit::Hz, 2000.f, 12000.f, 8000.f), L("Resonance", Unit::Percent, 0.f, 100.f, 50.f), X, X, X, X, X}},
    {"Chorus", "Modulation", 50.f, {C("Voices", kVoices, 1), G("Rate", Unit::Hz, 0.05f, 8.f, 0.6f), L("Depth", Unit::Percent, 0.f, 100.f, 40.f), G("Delay", Unit::Ms, 3.f, 30.f, 9.f),
        L("Feedback", Unit::Percent, 0.f, 80.f, 0.f), Width, L("Tone", Unit::Percent, 0.f, 100.f, 70.f), X, X, X, X, X}},
    {"Flanger", "Modulation", 50.f, {G("Rate", Unit::Hz, 0.02f, 10.f, 0.25f), C("Sync", kSync, 0), L("Depth", Unit::Percent, 0.f, 100.f, 70.f), G("Manual", Unit::Ms, 0.1f, 10.f, 1.5f),
        L("Feedback", Unit::Percent, -95.f, 95.f, 50.f), L("Stereo Phase", Unit::Degrees, 0.f, 180.f, 90.f), X, X, X, X, X, X}},
    {"Phaser", "Modulation", 50.f, {C("Stages", kStages, 1), G("Rate", Unit::Hz, 0.02f, 10.f, 0.3f), C("Sync", kSync, 0), L("Depth", Unit::Percent, 0.f, 100.f, 70.f),
        G("Centre", Unit::Hz, 100.f, 4000.f, 700.f), L("Feedback", Unit::Percent, -95.f, 95.f, 40.f), L("Stereo Phase", Unit::Degrees, 0.f, 180.f, 90.f), X, X, X, X, X}},
    {"Ensemble", "Modulation", 60.f, {G("Rate", Unit::Hz, 0.1f, 3.f, 0.6f), G("Shimmer Rate", Unit::Hz, 2.f, 12.f, 6.f), L("Depth", Unit::Percent, 0.f, 100.f, 60.f),
        Width, L("Tone", Unit::Percent, 0.f, 100.f, 60.f), X, X, X, X, X, X, X}},
    {"Tremolo / Pan", "Modulation", 100.f, {C("Mode", kTremModes, 0), G("Rate", Unit::Hz, 0.05f, 20.f, 4.f), C("Sync", kSync, 0), L("Depth", Unit::Percent, 0.f, 100.f, 60.f),
        C("Shape", kShapes, 0), L("Stereo Phase", Unit::Degrees, 0.f, 180.f, 0.f), G("Smoothing", Unit::Ms, 0.1f, 20.f, 2.f), X, X, X, X, X}},
    {"Compressor", "Dynamics", 100.f, {L("Threshold", Unit::Db, -60.f, 0.f, -18.f), G("Ratio", Unit::Ratio, 1.f, 20.f, 4.f), G("Attack", Unit::Ms, 0.05f, 200.f, 10.f),
        G("Release", Unit::Ms, 5.f, 3000.f, 150.f), L("Knee", Unit::Db, 0.f, 24.f, 6.f), L("Makeup", Unit::Db, 0.f, 24.f, 0.f), T("Auto Makeup", 0.f),
        C("Detector", kDetector, 0), G("Sidechain HPF", Unit::Hz, 20.f, 500.f, 20.f), L("Stereo Link", Unit::Percent, 0.f, 100.f, 100.f), C("Character", kCharacter, 0), X}},
    {"Limiter", "Dynamics", 100.f, {L("Input Gain", Unit::Db, 0.f, 24.f, 0.f), L("Ceiling", Unit::Db, -12.f, 0.f, -0.3f), G("Release", Unit::Ms, 1.f, 1000.f, 80.f),
        L("Stereo Link", Unit::Percent, 0.f, 100.f, 100.f), T("Soft Clip", 0.f), X, X, X, X, X, X, X}},
    {"Gate", "Dynamics", 100.f, {L("Threshold", Unit::Db, -90.f, 0.f, -45.f), L("Range", Unit::Db, -90.f, 0.f, -80.f), G("Attack", Unit::Ms, 0.05f, 50.f, 0.5f),
        G("Hold", Unit::Ms, 0.1f, 500.f, 20.f), G("Release", Unit::Ms, 5.f, 2000.f, 120.f), L("Hysteresis", Unit::Db, 0.f, 12.f, 3.f),
        G("Sidechain HPF", Unit::Hz, 20.f, 500.f, 20.f), G("Ratio", Unit::Ratio, 1.f, 10.f, 10.f), X, X, X, X}},
    {"Stereo Width", "Utility", 100.f, {L("Width", Unit::Percent, 0.f, 200.f, 100.f), L("Mid Gain", Unit::Db, -12.f, 12.f, 0.f), L("Side Gain", Unit::Db, -12.f, 12.f, 0.f),
        G("Bass Mono", Unit::Hz, 20.f, 500.f, 20.f), L("Pan", Unit::Percent, -100.f, 100.f, 0.f), L("Haas", Unit::Ms, 0.f, 30.f, 0.f), X, X, X, X, X, X}},
};
} // namespace

const TypeSpec& typeSpec(uint32_t type) noexcept { return kSpecs[type < FxTypeCount ? type : 0]; }

uint32_t paramCount(uint32_t type) noexcept {
    const TypeSpec& t = typeSpec(type);
    uint32_t n = 0;
    for (uint32_t i = 0; i < 12; ++i) if (t.p[i].name) n = i + 1;
    return n;
}

float mapValue(const ParamSpec& s, float n) noexcept {
    n = std::clamp(n, 0.f, 1.f);
    switch (s.curve) {
        case Curve::Log: return s.min * std::pow(s.max / s.min, n);
        case Curve::Choice: return std::round(n * (s.max - s.min) + s.min);
        case Curve::Toggle: return n >= 0.5f ? 1.f : 0.f;
        default: return s.min + n * (s.max - s.min);
    }
}

float unmapValue(const ParamSpec& s, float v) noexcept {
    if (!(s.max > s.min)) return 0.f;
    v = std::clamp(v, s.min, s.max);
    switch (s.curve) {
        case Curve::Log: return std::log(v / s.min) / std::log(s.max / s.min);
        case Curve::Toggle: return v >= 0.5f ? 1.f : 0.f;
        default: return (v - s.min) / (s.max - s.min);
    }
}

int formatValue(const ParamSpec& s, float v, char* out, uint32_t cap) noexcept {
    if (!out || cap == 0) return 0;
    switch (s.unit) {
        case Unit::Choice: {
            const int i = std::clamp(static_cast<int>(std::lround(v)), 0, s.choiceCount ? s.choiceCount - 1 : 0);
            return std::snprintf(out, cap, "%s", s.choices ? s.choices[i] : "");
        }
        case Unit::Toggle: return std::snprintf(out, cap, "%s", v >= 0.5f ? "On" : "Off");
        case Unit::Percent: return std::snprintf(out, cap, "%.0f %%", static_cast<double>(v));
        case Unit::Ms: return v >= 1000.f ? std::snprintf(out, cap, "%.2f s", static_cast<double>(v) / 1000.0)
                                          : std::snprintf(out, cap, v < 10.f ? "%.2f ms" : "%.0f ms", static_cast<double>(v));
        case Unit::Seconds: return std::snprintf(out, cap, "%.2f s", static_cast<double>(v));
        case Unit::Hz: return v >= 1000.f ? std::snprintf(out, cap, "%.2f kHz", static_cast<double>(v) / 1000.0)
                                          : std::snprintf(out, cap, v < 10.f ? "%.2f Hz" : "%.0f Hz", static_cast<double>(v));
        case Unit::Db: return std::snprintf(out, cap, "%+.1f dB", static_cast<double>(v));
        case Unit::Semitones: return std::snprintf(out, cap, "%+.1f st", static_cast<double>(v));
        case Unit::Ratio: return std::snprintf(out, cap, "%.2f", static_cast<double>(v));
        case Unit::Degrees: return std::snprintf(out, cap, "%.0f°", static_cast<double>(v));
        case Unit::Bits: return std::snprintf(out, cap, "%.1f bits", static_cast<double>(v));
        case Unit::PerSecond: return std::snprintf(out, cap, "%.1f /s", static_cast<double>(v));
        case Unit::Multiplier: return std::snprintf(out, cap, "×%.2f", static_cast<double>(v));
        default: return std::snprintf(out, cap, "%.0f", static_cast<double>(v));
    }
}

} // namespace vektur::fx
