// Graph.cpp — patch validation and compilation. See Graph.hpp.
#include "Graph.hpp"

#include <cmath>

namespace vektur {

namespace {
void reject(VKPatchResult& r, uint16_t id, VKRejectReason why) noexcept {
    if (r.rejected < 16) { r.rejectedIds[r.rejected] = id; r.rejectedReasons[r.rejected] = static_cast<uint8_t>(why); }
    ++r.rejected;
}
}

int32_t compilePatch(const VKCable* cables, uint32_t count, CompiledGraph& g, VKPatchResult& res) noexcept {
    const Rack& rk = rack();
    res = {};
    const uint32_t generation = g.generation + 1;
    g = CompiledGraph{};
    g.generation = generation;

    // ---- 1. validate user cables -------------------------------------------
    bool inputTaken[kMaxInstances][kMaxPorts]{};
    bool idUsed[VK_MAX_CABLE_ID]{};
    CompiledEdge user[VK_MAX_CABLES];
    uint32_t userCount = 0;
    for (uint32_t i = 0; i < count && cables; ++i) {
        const VKCable& c = cables[i];
        if (c.id >= VK_MAX_CABLE_ID) { reject(res, c.id, VK_REJECT_BAD_INDEX); continue; }
        if (idUsed[c.id]) { reject(res, c.id, VK_REJECT_DUPLICATE_ID); continue; }
        if (c.srcModule >= rk.count || c.dstModule >= rk.count) { reject(res, c.id, VK_REJECT_BAD_INDEX); continue; }
        const InstanceDesc& s = rk.inst[c.srcModule];
        const InstanceDesc& d = rk.inst[c.dstModule];
        if (s.hidden || d.hidden) { reject(res, c.id, VK_REJECT_BAD_INDEX); continue; }
        if (c.srcPort >= s.nOut || c.dstPort >= d.nIn) { reject(res, c.id, VK_REJECT_DIRECTION); continue; }
        if (!std::isfinite(c.gain) || std::fabs(c.gain) > 2.f) { reject(res, c.id, VK_REJECT_BAD_GAIN); continue; }
        if (inputTaken[c.dstModule][c.dstPort]) { reject(res, c.id, VK_REJECT_INPUT_TAKEN); continue; }
        if (userCount >= VK_MAX_CABLES) { reject(res, c.id, VK_REJECT_TOO_MANY); continue; }
        inputTaken[c.dstModule][c.dstPort] = true;
        idUsed[c.id] = true;
        CompiledEdge& e = user[userCount++];
        e = {};
        e.src = c.srcModule; e.srcPort = c.srcPort; e.dst = c.dstModule; e.dstPort = c.dstPort;
        e.cableGain = c.gain; e.slot = c.id; e.user = true;
    }
    res.accepted = userCount;

    // ---- 2. merge with normalled edges (a user cable breaks its input's normal)
    CompiledEdge all[kMaxEdges];
    uint32_t n = 0;
    for (uint32_t i = 0; i < rk.normalCount; ++i) {
        const NormalEdge& ne = rk.normals[i];
        if (inputTaken[ne.dstInst][ne.dstPort]) continue;
        CompiledEdge& e = all[n++];
        e = {};
        e.src = ne.srcInst; e.srcPort = ne.srcPort; e.dst = ne.dstInst; e.dstPort = ne.dstPort;
        e.gain = ne.gain; e.slot = static_cast<uint16_t>(VK_NORMAL_SLOT_BASE + i);
    }
    for (uint32_t i = 0; i < userCount; ++i) all[n++] = user[i];

    // ---- 3. needed modules: everything upstream of the output or of a user cable
    bool needed[kMaxInstances]{};
    needed[I_OUTPUT] = true;
    for (uint32_t i = 0; i < userCount; ++i) { needed[user[i].dst] = true; g.userOut[user[i].src] = true; }
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t i = 0; i < n; ++i)
            if (needed[all[i].dst] && !needed[all[i].src]) { needed[all[i].src] = true; changed = true; }
    }

    // ---- 4. topological order; break cycles with one-block feedback edges -----
    bool placed[kMaxInstances]{};
    bool fb[kMaxEdges]{};
    uint32_t remaining = 0;
    for (uint32_t m = 0; m < rk.count; ++m) remaining += needed[m] ? 1u : 0u;
    while (remaining > 0) {
        bool progress = false;
        for (uint32_t m = 0; m < rk.count; ++m) {
            if (!needed[m] || placed[m]) continue;
            bool ready = true;
            for (uint32_t i = 0; i < n && ready; ++i)
                if (!fb[i] && all[i].dst == m && needed[all[i].src] && !placed[all[i].src] && all[i].src != m) ready = false;
            for (uint32_t i = 0; i < n; ++i)   // self-loop is always a feedback edge
                if (all[i].dst == m && all[i].src == m) fb[i] = true;
            if (ready) { placed[m] = true; g.order[g.orderCount++] = static_cast<uint8_t>(m); --remaining; progress = true; }
        }
        if (!progress) {
            // Cycle: turn one blocking edge into a delayed feedback edge, preferring
            // user cables (the normalled voice itself is acyclic).
            int pick = -1;
            for (int pass = 0; pass < 2 && pick < 0; ++pass)
                for (uint32_t i = 0; i < n; ++i) {
                    const auto& e = all[i];
                    if (fb[i] || !needed[e.src] || placed[e.src] || placed[e.dst] || !needed[e.dst]) continue;
                    if (pass == 0 && !e.user) continue;
                    pick = static_cast<int>(i); break;
                }
            if (pick < 0 || res.feedbackEdges >= kMaxFeedback) {
                // Cannot break further (should not happen): drop remaining modules.
                break;
            }
            fb[pick] = true;
            ++res.feedbackEdges;
        }
    }

    // ---- 5. emit edges sorted by destination input ---------------------------
    for (uint32_t m = 0; m < rk.count; ++m) g.needed[m] = placed[m];
    for (uint32_t m = 0; m < rk.count; ++m)
        for (uint32_t p = 0; p < rk.inst[m].nIn; ++p) {
            g.inStart[m][p] = static_cast<uint16_t>(g.edgeCount);
            for (uint32_t i = 0; i < n; ++i) {
                if (all[i].dst != m || all[i].dstPort != p || !placed[all[i].src] || !placed[m]) continue;
                CompiledEdge e = all[i];
                if (fb[i]) e.feedback = static_cast<uint8_t>(g.feedbackCount++);
                g.edges[g.edgeCount++] = e;
                ++g.inCount[m][p];
            }
        }
    return static_cast<int32_t>(res.rejected);
}

} // namespace vektur
