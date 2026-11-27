// make_golden.cpp — writes golden/<case>.f32 (interleaved stereo float32).
// Run ONLY when intentionally re-baselining the sound.
#include "golden_cases.hpp"
#include <cstdio>
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "golden";
    for (auto& c : goldenCases()) {
        auto x = renderGolden(c);
        std::string p = std::string(dir) + "/" + c.name + ".f32";
        FILE* f = std::fopen(p.c_str(), "wb"); std::fwrite(x.data(), sizeof(float), x.size(), f); std::fclose(f);
        std::printf("%s (%zu frames)\n", p.c_str(), x.size() / 2);
    }
}
