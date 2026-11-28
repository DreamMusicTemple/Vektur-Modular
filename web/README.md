# Vektur Modular — web test build

The instrument's C++ kernel compiled to WebAssembly and played in the browser,
behind a hardware-panel UI (Phase 8 design language).

```bash
tools/build_web.sh          # kernel.wasm (clang wasm32 + wasm-ld), params.json
python3 tools/panels.py     # panels.json (shared with the SwiftUI rack)
node tools/test_web.mjs     # smoke test in Node
python3 -m http.server -d web 8000   # open http://localhost:8000
```

Files: `index.html` (layout and style), `app.js` (UI, keyboard, Web MIDI,
patching, MIDI Learn, presets), `engine.js` (kernel driver, shared by the
AudioWorklet and the ScriptProcessor fallback), `worklet.js`, `runtime/`
(the wasm runtime shim and C glue). `kernel.wasm`, `params.json` and
`panels.json` are generated — do not edit them.
