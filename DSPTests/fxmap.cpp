// fxmap.cpp — tool for tools/factory_presets.py: dumps the effect
// catalogue (--catalogue) or converts real effect parameter values to the
// normalised slot values (stdin: "type index value" per line → normalised).
#include "VekturDSP.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "--catalogue")) {
        for (uint32_t t = 0; t < vk_fx_type_count(); ++t) {
            VKFxTypeInfo ti; vk_fx_type_info(t, &ti);
            std::printf("%u %s:", t, ti.name);
            for (uint32_t k = 0; k < 12; ++k) { VKFxParamInfo pi; if (vk_fx_param_info(t, k, &pi) && pi.name) std::printf(" [%u]%s", k, pi.name); }
            std::printf("\n");
        }
        return 0;
    }
    if (argc > 1 && !std::strcmp(argv[1], "--defaults")) {   // "type index normalisedDefault" for every named parameter
        for (uint32_t t = 0; t < vk_fx_type_count(); ++t)
            for (uint32_t k = 0; k < 12; ++k) { VKFxParamInfo pi; if (vk_fx_param_info(t, k, &pi) && pi.name) std::printf("%u %u %.6f\n", t, k, pi.defaultNormalised); }
        return 0;
    }
    unsigned t, k; float v;
    while (std::scanf("%u %u %f", &t, &k, &v) == 3) std::printf("%.6f\n", vk_fx_unmap_value(t, k, v));
}
