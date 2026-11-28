// preset_audit.cpp — prints one JSON line per factory preset (loudness, peak)
// for tools/factory_presets.py's gain staging.
#include "preset_audition.hpp"
#include <cstdio>
int main() {
    for (size_t i = 0; i < kFactory.size(); ++i) {
        const Audition a = audition(kFactory[i]);
        std::printf("{\"i\": %zu, \"name\": \"%s\", \"rms\": %.2f, \"peak\": %.2f, \"finite\": %s, \"rejected\": %d}\n", i, kFactory[i].name, a.rms, a.peak, a.finite ? "true" : "false", a.rejectedCables);
    }
}
