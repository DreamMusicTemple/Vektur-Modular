# Golden renders — Phase 2 engine reference

`*.f32` are interleaved stereo float32 renders of the cases in `golden_cases.hpp`,
produced by the **Phase 2** engine (fixed voice, before the modular graph) with
**clang 18, -O2, x86-64 Linux**. `test_graph` checks that the Phase 3+ normalled
graph reproduces them.

Floating-point rounding (exp2, tan, FMA contraction) differs between compilers
and build modes, and oscillator phase accumulates those differences, so the
comparison is only meaningful on the same toolchain. `test_graph` detects a
mismatch on the `default` case and prints `SKIP` instead of passing or failing.

Evidence that the graph itself is equivalent across toolchains (2026-11-27):
Phase 3 built with g++ 13 vs Phase 2 built with g++ 13 → 10 cases bit-close
(−100 to −300 dB), `a_to_b` and `vco3_mod` different by design (see PHASE3A_REPORT).

To re-baseline on another toolchain (e.g. Xcode), build `make_golden.cpp` against the
Phase 2 release (`VekturModular-Phase2.zip`) with that toolchain and copy the
output here.
