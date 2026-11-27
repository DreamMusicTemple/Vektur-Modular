// EffectsEngine.cpp — see EffectsEngine.hpp.
#include "EffectsEngine.hpp"

#include <cstring>

namespace vektur::fx {

namespace {
constexpr float kSwapStep = 1.f / 144.f;     // ≈ 3 ms at 48 kHz
constexpr float kRouteStep = 1.f / 128.f;
inline uint32_t clampLat(uint32_t v) noexcept { return std::min(v, EffectsEngine::kMaxCompensation); }
}

bool EffectsEngine::Routing::operator==(const Routing& o) const noexcept {
    return std::memcmp(bus, o.bus, sizeof bus) == 0 && insertPar == o.insertPar && aPar == o.aPar && bPar == o.bPar &&
           aPost == o.aPost && bPost == o.bPost && std::memcmp(lat, o.lat, sizeof lat) == 0;
}

EffectsEngine::~EffectsEngine() {
    for (auto& s : slots_) {
        delete s.cur; s.cur = nullptr;
        Effect* p = s.pending.exchange(nullptr);
        if (p != reinterpret_cast<Effect*>(uintptr_t{1})) delete p;
    }
    collect();
}

int EffectsEngine::structuralOf(uint32_t type, uint32_t slot, const ParameterStore& p) const noexcept {
    const TypeSpec& t = typeSpec(type);
    for (uint32_t i = 0; i < 12; ++i)
        if (t.p[i].name && t.p[i].structural) return static_cast<int>(mapValue(t.p[i], p.get(slotAddr(slot, 5 + i))));
    return -1;
}

Effect* EffectsEngine::build(uint32_t slot, const ParameterStore& p, Built& sig) {
    const auto type = static_cast<uint32_t>(std::clamp(std::lround(p.get(slotAddr(slot, 0))), 0L, static_cast<long>(FxTypeCount - 1)));
    sig.type = type;
    sig.structural = structuralOf(type, slot, p);
    sig.irGen = irGen_[slot];
    return makeEffect(type, sr_, maxFrames_, sig.structural, userIR_[slot].get()).release();
}

void EffectsEngine::allocate(float sampleRate, uint32_t maxFrames, const ParameterStore& p) {
    sr_ = sampleRate; maxFrames_ = maxFrames;
    for (auto* v : {&dryL_, &dryR_, &mainL_, &mainR_, &aL_, &aR_, &bL_, &bR_, &tL_, &tR_, &accL_, &accR_, &sdL_, &sdR_}) v->assign(maxFrames, 0.f);
    for (int c = 0; c < 2; ++c) {
        alignMain_[c].allocate(kMaxCompensation * 2); alignA_[c].allocate(kMaxCompensation * 2);
        alignB_[c].allocate(kMaxCompensation * 2); bypass_[c].allocate(kMaxCompensation * 2);
    }
    // User IRs were resampled for the old rate: re-resample.
    for (uint32_t s = 0; s < kSlots; ++s) {
        if (userIR_[s] && userIR_[s]->sampleRate != sampleRate) {
            auto old = std::move(userIR_[s]);
            setUserIR(s, old->left.data(), old->right.data(), static_cast<uint32_t>(old->left.size()), old->sampleRate);
        }
    }
    for (uint32_t s = 0; s < kSlots; ++s) {
        Slot& sl = slots_[s];
        delete sl.cur; sl.cur = nullptr;
        Effect* pend = sl.pending.exchange(nullptr);
        if (pend != reinterpret_cast<Effect*>(uintptr_t{1})) delete pend;
        for (int c = 0; c < 2; ++c) { sl.dry[c].allocate(kMaxCompensation); sl.align[c].allocate(kMaxCompensation); }
        sl.cur = build(s, p, built_[s]);
        sl.type = sl.cur ? sl.cur->fxType : 0;
        sl.lat = sl.cur ? clampLat(sl.cur->latency()) : 0;
        sl.swap = 0; sl.swapGain = 1.f; sl.enGain = p.get(slotAddr(s, 1)) >= 0.5f ? 1.f : 0.f; sl.silent = 0;
        types_[s].store(sl.type, std::memory_order_relaxed);
    }
    collect();
    routing_ = readRouting(p);
    routingFading_ = false; routeGain_ = 1.f;
    globalEn_ = p.get(kBusBase) >= 0.5f ? 1.f : 0.f;
    reset();
}

uint32_t EffectsEngine::update(const ParameterStore& p) {
    collect();
    uint32_t rebuilt = 0;
    for (uint32_t s = 0; s < kSlots; ++s) {
        const auto type = static_cast<uint32_t>(std::clamp(std::lround(p.get(slotAddr(s, 0))), 0L, static_cast<long>(FxTypeCount - 1)));
        const int structural = structuralOf(type, s, p);
        const bool irMatters = type == FxConvolution && structural >= static_cast<int>(kBuiltInIRCount);
        const Built& b = built_[s];
        if (b.type == type && b.structural == structural && (!irMatters || b.irGen == irGen_[s])) continue;
        Effect* e = build(s, p, built_[s]);
        // An Empty slot still needs a (null) swap so the old effect fades out: use a sentinel.
        Effect* prev = slots_[s].pending.exchange(e ? e : reinterpret_cast<Effect*>(uintptr_t{1}));
        if (prev && prev != reinterpret_cast<Effect*>(uintptr_t{1})) delete prev;   // never adopted
        ++rebuilt;
    }
    return rebuilt;
}

void EffectsEngine::setUserIR(uint32_t slot, const float* left, const float* right, uint32_t frames, float irSampleRate) {
    if (slot >= kSlots || !left || frames == 0 || !(irSampleRate > 0.f)) return;
    auto ir = std::make_unique<ImpulseResponse>();
    const double ratio = static_cast<double>(irSampleRate) / sr_;
    const auto maxOut = static_cast<size_t>(3.0 * sr_);
    const auto outN = std::min(maxOut, static_cast<size_t>(std::ceil(frames / ratio)));
    ir->left.resize(outN); ir->right.resize(outN);
    // Cubic (Hermite) resampling; a one-pole pre-filter tames aliasing when downsampling.
    std::vector<float> src[2] = {std::vector<float>(left, left + frames), std::vector<float>(right ? right : left, (right ? right : left) + frames)};
    if (ratio > 1.0) {
        OnePole lp; lp.set(0.45f * sr_, irSampleRate);
        for (auto& v : src) { lp.reset(); for (float& x : v) x = lp.lp(x); }
    }
    for (int c = 0; c < 2; ++c) {
        std::vector<float>& out = c ? ir->right : ir->left;
        const std::vector<float>& in = src[c];
        auto at = [&](long i) { return i < 0 || i >= static_cast<long>(in.size()) ? 0.f : in[static_cast<size_t>(i)]; };
        for (size_t k = 0; k < outN; ++k) {
            const double pos = static_cast<double>(k) * ratio;
            const long i = static_cast<long>(pos); const float f = static_cast<float>(pos - static_cast<double>(i));
            const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            out[k] = ((c3 * f + c2) * f + c1) * f + x0;
        }
    }
    ir->sampleRate = sr_;
    ir->generation = ++irGen_[slot];
    userIR_[slot] = std::move(ir);
}

void EffectsEngine::clearUserIR(uint32_t slot) {
    if (slot >= kSlots) return;
    userIR_[slot].reset();
    ++irGen_[slot];
}

void EffectsEngine::collect() {
    Effect* e;
    while (graveyard_.pop(e)) delete e;
}

void EffectsEngine::reset() noexcept {
    for (auto& s : slots_) {
        if (s.cur) s.cur->reset();
        for (int c = 0; c < 2; ++c) { s.dry[c].reset(); s.align[c].reset(); }
        s.silent = 0;
    }
    for (int c = 0; c < 2; ++c) { alignMain_[c].reset(); alignA_[c].reset(); alignB_[c].reset(); bypass_[c].reset(); }
}

EffectsEngine::Routing EffectsEngine::readRouting(const ParameterStore& p) const noexcept {
    Routing r;
    for (uint32_t s = 0; s < kSlots; ++s) {
        // Empty slots are on no bus: they must not count as parallel branches.
        r.bus[s] = slots_[s].cur ? static_cast<uint8_t>(std::clamp(std::lround(p.get(slotAddr(s, 4))), 0L, 2L)) : uint8_t{255};
        r.lat[s] = slots_[s].cur ? slots_[s].lat : 0;
    }
    r.insertPar = p.get(kBusBase + 1) >= 0.5f; r.aPar = p.get(kBusBase + 2) >= 0.5f; r.bPar = p.get(kBusBase + 3) >= 0.5f;
    r.aPost = p.get(kBusBase + 6) >= 0.5f; r.bPost = p.get(kBusBase + 7) >= 0.5f;
    return r;
}

void EffectsEngine::adopt(uint32_t s) noexcept {
    Slot& sl = slots_[s];
    Effect* next = sl.pending.load(std::memory_order_acquire);
    if (!next) return;
    if (sl.cur && !graveyard_.push(sl.cur)) return;   // graveyard full: try again next span
    next = sl.pending.exchange(nullptr, std::memory_order_acq_rel);
    if (next == reinterpret_cast<Effect*>(uintptr_t{1})) next = nullptr;
    sl.cur = next;
    sl.type = next ? next->fxType : 0;
    sl.lat = next ? clampLat(next->latency()) : 0;
    sl.silent = 0;
    for (int c = 0; c < 2; ++c) { sl.dry[c].reset(); sl.align[c].reset(); }
    if (next) { sl.swap = 2; sl.swapGain = 0.f; } else { sl.swap = 0; sl.swapGain = 1.f; }
}

void EffectsEngine::runSlot(uint32_t s, float* L, float* R, uint32_t n, const ParameterStore& p, const FxContext& c) noexcept {
    Slot& sl = slots_[s];
    const uint32_t type = sl.cur ? sl.type : 0;
    // Dry copy, delayed by the slot's latency so dry/wet stay phase-aligned.
    for (uint32_t i = 0; i < n; ++i) { sdL_[i] = sl.dry[0].process(L[i], sl.lat); sdR_[i] = sl.dry[1].process(R[i], sl.lat); }
    const bool enabled = p.get(slotAddr(s, 1)) >= 0.5f;
    if (!sl.cur) {   // empty slot: pass through (after any fade-out of the effect it replaced)
        std::memcpy(L, sdL_.data(), sizeof(float) * n); std::memcpy(R, sdR_.data(), sizeof(float) * n);
        return;
    }
    const TypeSpec& t = typeSpec(type);
    FxParams fp;
    for (uint32_t k = 0; k < 12; ++k) {
        const ParamSpec& ps = t.p[k];
        if (!ps.name) continue;
        const uint64_t a = slotAddr(s, 5 + k);
        fp.v[k] = mapValue(ps, (ps.curve == Curve::Choice || ps.curve == Curve::Toggle) ? p.get(a) : p.smoothed(a));
    }
    // Silence: after the input has been silent for longer than the tail, skip the effect.
    float peak = 0.f;
    for (uint32_t i = 0; i < n; ++i) peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
    sl.silent = peak < 1e-7f ? sl.silent + n : 0;
    const float tail = sl.cur->tailSeconds(fp);
    const bool skip = tail >= 0.f && static_cast<double>(sl.silent) > static_cast<double>(tail) * sr_ + sl.lat + 2.0 * n;
    if (skip) { std::memset(L, 0, sizeof(float) * n); std::memset(R, 0, sizeof(float) * n); }
    else {
        sl.cur->process(L, R, n, fp, c);
        bool finite = true;
        for (uint32_t i = 0; i < n; ++i) finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
        if (!finite) {
            sl.cur->reset();
            std::memset(L, 0, sizeof(float) * n); std::memset(R, 0, sizeof(float) * n);
            nonFinite_.store(true, std::memory_order_relaxed);
        }
    }
    meters_[s].store(sl.cur->meter(), std::memory_order_relaxed);
    const float mix = p.smoothed(slotAddr(s, 2)) / 100.f, out = dbToGain(p.smoothed(slotAddr(s, 3)));   // 100 % → exactly 1
    const float enStep = 1.f / (0.01f * sr_);
    for (uint32_t i = 0; i < n; ++i) {
        const float wl = (L[i] * mix + sdL_[i] * (1.f - mix)) * out, wr = (R[i] * mix + sdR_[i] * (1.f - mix)) * out;
        sl.enGain = clampf(sl.enGain + (enabled ? enStep : -enStep), 0.f, 1.f);
        float g = sl.enGain;
        if (sl.swap == 1) sl.swapGain = std::max(0.f, sl.swapGain - kSwapStep);
        else if (sl.swap == 2) { sl.swapGain = std::min(1.f, sl.swapGain + kSwapStep); if (sl.swapGain >= 1.f) sl.swap = 0; }
        g *= sl.swapGain;
        // Written so g = 1 (and mix = 100 %) give the effect output exactly.
        L[i] = wl * g + sdL_[i] * (1.f - g);
        R[i] = wr * g + sdR_[i] * (1.f - g);
    }
}

uint32_t EffectsEngine::runChain(uint8_t bus, bool parallel, const float* inL, const float* inR, float* outL, float* outR,
                                 uint32_t n, const ParameterStore& p, const FxContext& c) noexcept {
    uint32_t chainLat = 0; int count = 0;
    for (uint32_t s = 0; s < kSlots; ++s) if (routing_.bus[s] == bus) { chainLat = parallel ? std::max(chainLat, routing_.lat[s]) : chainLat + routing_.lat[s]; ++count; }
    if (!parallel || count <= 1) {
        if (outL != inL) { std::memcpy(outL, inL, sizeof(float) * n); std::memcpy(outR, inR, sizeof(float) * n); }
        for (uint32_t s = 0; s < kSlots; ++s) if (routing_.bus[s] == bus) runSlot(s, outL, outR, n, p, c);
        return clampLat(chainLat);
    }
    std::memset(accL_.data(), 0, sizeof(float) * n); std::memset(accR_.data(), 0, sizeof(float) * n);
    for (uint32_t s = 0; s < kSlots; ++s) {
        if (routing_.bus[s] != bus) continue;
        std::memcpy(tL_.data(), inL, sizeof(float) * n); std::memcpy(tR_.data(), inR, sizeof(float) * n);
        runSlot(s, tL_.data(), tR_.data(), n, p, c);
        const uint32_t d = chainLat - routing_.lat[s];
        Slot& sl = slots_[s];
        for (uint32_t i = 0; i < n; ++i) { accL_[i] += sl.align[0].process(tL_[i], d); accR_[i] += sl.align[1].process(tR_[i], d); }
    }
    const float k = 1.f / static_cast<float>(count);
    for (uint32_t i = 0; i < n; ++i) { outL[i] = accL_[i] * k; outR[i] = accR_[i] * k; }
    return clampLat(chainLat);
}

void EffectsEngine::process(float* L, float* R, uint32_t n, const ParameterStore& p, const FxContext& c) noexcept {
    if (n == 0 || n > maxFrames_) return;
    // Adopt rebuilt effects: fade the slot out, swap, fade in.
    for (uint32_t s = 0; s < kSlots; ++s) {
        Slot& sl = slots_[s];
        if (sl.swap == 0 && sl.pending.load(std::memory_order_acquire)) sl.swap = 1;
        if (sl.swap == 1 && (sl.swapGain <= 0.f || !sl.cur)) adopt(s);   // an empty slot needs no fade-out
        types_[s].store(sl.cur ? sl.type : 0, std::memory_order_relaxed);
    }
    // Routing / latency changes: brief fade of the whole effects output.
    const Routing want = readRouting(p);
    if (!(want == routing_)) {
        if (!routingFading_) routingFading_ = true;
        if (routeGain_ <= 0.f) {
            routing_ = want;
            routingFading_ = false;
            for (int ch = 0; ch < 2; ++ch) { alignMain_[ch].reset(); alignA_[ch].reset(); alignB_[ch].reset(); bypass_[ch].reset(); }
        }
    }
    bool any = false;
    for (uint32_t s = 0; s < kSlots; ++s) any |= slots_[s].cur != nullptr || slots_[s].swap != 0;
    const float enTarget = p.get(kBusBase) >= 0.5f ? 1.f : 0.f;
    if (!any && total_ == 0 && routeGain_ >= 1.f && !routingFading_) { globalEn_ = enTarget; return; }   // nothing inserted: bit-exact pass-through

    std::memcpy(dryL_.data(), L, sizeof(float) * n); std::memcpy(dryR_.data(), R, sizeof(float) * n);
    latMain_ = runChain(0, routing_.insertPar, dryL_.data(), dryR_.data(), mainL_.data(), mainR_.data(), n, p, c);
    bool hasA = false, hasB = false;
    for (uint32_t s = 0; s < kSlots; ++s) { hasA |= routing_.bus[s] == 1; hasB |= routing_.bus[s] == 2; }
    const float sendA = p.smoothed(kBusBase + 4) * 0.01f, sendB = p.smoothed(kBusBase + 5) * 0.01f;
    uint32_t pathA = 0, pathB = 0;
    if (hasA) {
        const float* src[2] = {routing_.aPost ? mainL_.data() : dryL_.data(), routing_.aPost ? mainR_.data() : dryR_.data()};
        for (uint32_t i = 0; i < n; ++i) { aL_[i] = src[0][i] * sendA; aR_[i] = src[1][i] * sendA; }
        latA_ = runChain(1, routing_.aPar, aL_.data(), aR_.data(), aL_.data(), aR_.data(), n, p, c);
        pathA = latA_ + (routing_.aPost ? latMain_ : 0);
    }
    if (hasB) {
        const float* src[2] = {routing_.bPost ? mainL_.data() : dryL_.data(), routing_.bPost ? mainR_.data() : dryR_.data()};
        for (uint32_t i = 0; i < n; ++i) { bL_[i] = src[0][i] * sendB; bR_[i] = src[1][i] * sendB; }
        latB_ = runChain(2, routing_.bPar, bL_.data(), bR_.data(), bL_.data(), bR_.data(), n, p, c);
        pathB = latB_ + (routing_.bPost ? latMain_ : 0);
    }
    total_ = std::min(kMaxCompensation, std::max(latMain_, std::max(pathA, pathB)));
    latency_.store(total_, std::memory_order_relaxed);

    const float dryLvl = p.smoothed(kBusBase + 10) <= -59.9f ? 0.f : dbToGain(p.smoothed(kBusBase + 10));
    const float retA = p.smoothed(kBusBase + 8) <= -59.9f ? 0.f : dbToGain(p.smoothed(kBusBase + 8));
    const float retB = p.smoothed(kBusBase + 9) <= -59.9f ? 0.f : dbToGain(p.smoothed(kBusBase + 9));
    const float outG = dbToGain(p.smoothed(kBusBase + 11));
    const uint32_t dM = total_ - std::min(total_, latMain_), dA = total_ - std::min(total_, pathA), dB = total_ - std::min(total_, pathB);
    const float enStep = 1.f / (0.01f * sr_);
    for (uint32_t i = 0; i < n; ++i) {
        float l = alignMain_[0].process(mainL_[i], dM) * dryLvl, r = alignMain_[1].process(mainR_[i], dM) * dryLvl;
        if (hasA) { l += alignA_[0].process(aL_[i], dA) * retA; r += alignA_[1].process(aR_[i], dA) * retA; }
        if (hasB) { l += alignB_[0].process(bL_[i], dB) * retB; r += alignB_[1].process(bR_[i], dB) * retB; }
        l *= outG; r *= outG;
        const float bl = bypass_[0].process(L[i], total_), br = bypass_[1].process(R[i], total_);
        if (globalEn_ < enTarget) globalEn_ = std::min(enTarget, globalEn_ + enStep);
        else if (globalEn_ > enTarget) globalEn_ = std::max(enTarget, globalEn_ - enStep);
        l = l * globalEn_ + bl * (1.f - globalEn_); r = r * globalEn_ + br * (1.f - globalEn_);
        routeGain_ = clampf(routeGain_ + (routingFading_ ? -kRouteStep : kRouteStep), 0.f, 1.f);
        L[i] = l * routeGain_; R[i] = r * routeGain_;
    }
}

} // namespace vektur::fx
