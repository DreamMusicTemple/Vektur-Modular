# Performance — targets, measurements, method (Phase 10)

## Targets

| Item | Target | Status |
|---|---|---|
| Render thread | no allocation, lock, log or syscall | **Met.** Every suite tracks `operator new` during render; the recorder tap test asserts 0 allocations with the tap armed. ASan, UBSan and TSan are clean on all 10 suites. |
| Engine CPU, 8 voices, default patch | < 10 % of one performance core on A15 or newer at 48 kHz / 256 | Linux reference host: **6.1 %** (see below). Device figures are filled in by the on-device pass (table below). |
| Engine CPU, 16 voices | < 20 % (A15) | Reference host: **12.2 %**. |
| Worst case shipped (32 voices) | < 40 % (A15) | Reference host: **23.0 %**. |
| Memory | < 60 MB resident for the AUv3 in a host | Engine with 32 voices, all modules and 8 effect slots: about **21 MB** peak resident on the reference host (the whole test process). The recording ring (4 MB) is only allocated when recording first starts. |
| Added latency | 0 samples by default | **0.** Look-ahead effects (limiter, convolution) report their latency to the host and every path is compensated; `AUAudioUnit.latency` reflects it. |
| Recording | no dropouts | **Met by design and test:** the audio thread only copies into a wait-free 2^19-frame ring (≈ 11 s at 48 kHz). The writer drains it every 40 ms. Overflow drops are counted, never blocking. The test drains concurrently for 2,000 blocks under TSan and checks every frame was read or counted. |

## Reference measurements

`DSPTests/test_phase10 --bench` (best of four runs). Host: Intel Xeon @ 2.1 GHz, 2 vCPU, Linux, g++ -O2. Settings: 48 kHz, 256-frame buffers, VCO 2 and 3 at 80 % and 60 %, notes held throughout.

| Configuration | Share of one core |
|---|---:|
| 8 voices, default patch | 6.1 % |
| 16 voices | 12.2 % |
| 16 voices, HQ oscillators | 15.5 % |
| 16 voices, filters 4× oversampled | 17.2 % |
| 32 voices | 23.0 % |

Apple-silicon cores are typically 1.5–3× faster per clock than this virtual host for this kind of scalar float code. The table below is completed on devices with Instruments (Time Profiler + os_signpost "render" intervals, which the AU already emits in Debug builds).

| Device | 8 v | 16 v | 16 v HQ | 32 v | Memory (AU in AUM) |
|---|---:|---:|---:|---:|---:|
| iPhone 13 (A15) | — | — | — | — | — |
| iPad Air M2 | — | — | — | — | — |
| MacBook Air M1 (AU in Logic) | — | — | — | — | — |

## Quality options and their cost

- **HQ oscillators** (`global.oscillatorQuality`). The VCOs run at 2× through the half-band oversampler. Measured inharmonic (alias) energy for a held saw: note 96 goes from −39.4 dB to −46.7 dB, and note 103 from −36.9 dB to −46.4 dB, relative to the harmonics. The level is unchanged (0.00 dB at C4). Cost: about 27 % more engine CPU at 16 voices.
- **Filter oversampling** 1×/2×/4× (existing).
- **Battery Saving Mode** (UI). Display refresh drops from 30 Hz to 15 Hz (meters, telemetry, cable animation), cable sway, flow dashes and tilt motion stop, and the engine uses Standard oscillators with 1× filters. The previous quality settings are restored when it is turned off.

## Phase 10 engine work

- **Oversampler evaluation-order bug fixed.** `Oversampler::process` called its stateful callback inside a function's argument list, so the order of the two (or four) sub-samples was unspecified. GCC's release build happened to evaluate them in order; the sanitizer builds did not, and HQ then measured *worse* than Standard. The calls are now sequenced explicitly. This also fixes the 2×/4× filter path for any compiler that evaluates right to left. The golden renders are unchanged.
- **vDSP FFT.** On Apple platforms `fx/FFT.hpp` uses `vDSP_fft_zrip` for the convolution reverb and the spectral effect, with the same scaling as before. The portable radix-2 code remains the Linux and WebAssembly path. `vk_fft_selftest()` compares either path against a direct DFT: the error is 3.6e-7 at 2048 points on the portable path. `Phase10DSPTests` runs it on Apple, where it tests vDSP.
- **SIMD across voices: not done in 1.0, by decision.** Each voice runs a patchable graph (any cable, per-voice modulation, feedback with one sample of delay). Packing 4 or 8 voices into SIMD lanes needs every module to run in lane-parallel form, with lanes masked for voices that are idle or patched differently. That is a rewrite of every module kernel and of the graph scheduler. The measured headroom (16 voices at 12 % on a slow virtual core) does not justify that risk for 1.0. The plan for 1.1: SIMD-ise the three hottest kernels (VCO polyBLEP, ladder filter, envelope) across voices that share a graph — the default normalled patch is the common case. A scalar fallback is kept for patched voices.
- **Per-voice swap fade: deferred.** A patch change already crossfades the whole graph over 1.3 ms, which is click-free in all tests. A per-voice fade only matters when voices hold different graphs, and that becomes possible only with the 1.1 SIMD work.

## Profiling method

1. Build Release and run the AU in AUM (iOS) or Logic (macOS) at 48 kHz / 256 frames.
2. In Instruments, use Time Profiler plus the os_signpost "render" interval. Report the 99th-percentile render time as a share of the buffer duration.
3. Measure memory with Allocations (persistent bytes) after loading the heaviest factory preset (32 voices + convolution).
4. Run the DAW test matrix (`docs/DAW_TEST_MATRIX.md`) at 64-frame buffers to find the shortest stable buffer size.
