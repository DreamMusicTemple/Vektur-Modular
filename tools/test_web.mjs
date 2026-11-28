// test_web.mjs — smoke test of the browser build in Node (no browser needed):
// the wasm kernel loads, plays a chord through the arpeggiator and an
// effect, patches a cable, answers MIDI 2.0, and reports status.
//   node tools/test_web.mjs
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const { Engine } = await import(path.join(root, 'web/engine.js'));
const msgs = [];
const e = new Engine(fs.readFileSync(path.join(root, 'web/kernel.wasm')), 48000, (m) => msgs.push(m));
for (let i = 0; i < 100 && !msgs.some((m) => m.t === 'ready'); i++) await new Promise((r) => setTimeout(r, 20));
let fails = 0; const check = (c, s) => { console.log(`${c ? '  ok  ' : '  FAIL'} ${s}`); if (!c) fails++; };
check(msgs.find((m) => m.t === 'ready')?.ok, 'kernel instantiated and allocated');
e.message({ t: 'set', a: 36 * 256, v: 1 });
e.message({ t: 'fxtype', slot: 0, type: 6 });
e.message({ t: 'transport', playing: true, tempo: 120, num: 3, den: 4 });
e.message({ t: 'patch', cables: [[0, 32, 0, 13, 3, 1]] });
for (const n of [60, 64, 67]) e.message({ t: 'midi', d: [0x90, n, 100] });
e.message({ t: 'ump', w0: 0x40900000 | (72 << 8), w1: 0x80000000 });
const L = new Float32Array(128), R = new Float32Array(128);
let peak = 0, finite = true;
for (let b = 0; b < 375; b++) { e.render(L, R); for (const v of L) { peak = Math.max(peak, Math.abs(v)); finite &&= Number.isFinite(v); } }
check(finite && peak > 0.01 && peak < 2, `audible, finite output (peak ${peak.toFixed(3)})`);
check(msgs.find((m) => m.t === 'patched')?.rejected === 0, 'cable accepted');
const st = msgs.filter((m) => m.t === 'status').pop();
check(st && st.seq.flags & 1 && st.voices > 0, 'arpeggiator running, voices sounding');
check(st && st.midi.inN >= 4, 'MIDI 1.0 and 2.0 input counted');
check(msgs.some((m) => m.t === 'values' && m.pairs.length === 17), 'effect type change reports the slot values');
console.log(`\n${fails ? fails + ' failure(s)' : 'all web checks passed'}`);
process.exit(fails ? 1 : 0);
