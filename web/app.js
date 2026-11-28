// app.js — Vektur Modular web test build: hardware-panel UI (Phase 8 panel
// language, from panels.json shared with the SwiftUI rack), on-screen and
// computer keyboard, Web MIDI, patching, MIDI Learn and presets, over the
// real kernel (kernel.wasm) running in an AudioWorklet.
import { Engine, wasmEnv, readLayout } from './engine.js';

{
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const store = {
  get(k, d) { try { const v = localStorage.getItem('vektur.' + k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('vektur.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
};
let toastTimer;
function toast(msg) { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove('on'), 2600); }

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------
let P = {}, byAddr = new Map(), groups = {}, panels, virtualDests = {};
const values = new Map();          // address → value (mirror of the kernel)
const views = new Map();           // address → [update(v)]
let ui = null, L = null, factory = null;           // UI-side kernel instance (catalogue, formatting, chords, rack)
let rack = [];                     // [{key,name,ins:[{key,name,kind}],outs:[...] }]
let engine = { post() {} };
let manual = [];                   // manual chapters (manual.json)        // audio engine proxy
let audio = null, wasmBytes = null;

function cstr(ptr) { if (!ptr) return ''; const m = new Uint8Array(ui.memory.buffer); let e = ptr; while (m[e]) e++; return new TextDecoder().decode(m.subarray(ptr, e)); }

function norm(p, v) { if (p.s) return p.hi > p.lo ? (v - p.lo) / (p.hi - p.lo) : 0; if (p.log && p.lo > 0) return Math.log(Math.max(v, p.lo) / p.lo) / Math.log(p.hi / p.lo); return (v - p.lo) / (p.hi - p.lo); }
function denorm(p, n) { n = clamp(n, 0, 1); if (p.log && p.lo > 0) return p.lo * Math.pow(p.hi / p.lo, n); return p.lo + n * (p.hi - p.lo); }
function quant(p, v) { v = clamp(v, p.lo, p.hi); return p.s || p.u === 'boolean' || p.u === 'indexed' ? Math.round(v) : v; }

function fxSlotOf(a) { const m = Math.floor(a / 256); return m >= 39 && m <= 46 ? { slot: m - 39, i: a % 256 } : null; }
function fmt(p, v) {
  const fs = fxSlotOf(p.a);
  if (fs && fs.i >= 5 && ui) { const type = Math.round(values.get((39 + fs.slot) * 256) || 0); return cstr(ui.vkw_fx_format(type, fs.i - 5, v)) || '–'; }
  if (p.s) return p.s[Math.round(v)] ?? String(Math.round(v));
  const a = Math.abs(v);
  switch (p.u) {
    case 'boolean': return v >= 0.5 ? 'on' : 'off';
    case 'decibels': return v <= -60 ? '−∞ dB' : `${v.toFixed(1)} dB`;
    case 'hertz': return a >= 1000 ? `${(v / 1000).toFixed(2)} kHz` : a >= 100 ? `${v.toFixed(0)} Hz` : `${v.toFixed(a < 10 ? 2 : 1)} Hz`;
    case 'milliseconds': return a >= 1000 ? `${(v / 1000).toFixed(2)} s` : `${v.toFixed(a < 10 ? 1 : 0)} ms`;
    case 'seconds': return `${v.toFixed(2)} s`;
    case 'percent': return `${v.toFixed(0)} %`;
    case 'cents': return `${v >= 0 ? '+' : '−'}${a.toFixed(0)} ct`;
    case 'semitones': return `${v >= 0 ? '+' : '−'}${a.toFixed(a % 1 ? 1 : 0)} st`;
    case 'octaves': return `${v >= 0 ? '+' : '−'}${a.toFixed(2)} oct`;
    case 'degrees': return `${v.toFixed(0)}°`;
    default: return a >= 100 ? v.toFixed(0) : v.toFixed(2);
  }
}
// Short silkscreen legend: drop the module words the panel already carries.
function legend(p) {
  let n = p.n;
  const g = (groups[p.g] || '').replace(/\(.*\)/, '').trim();
  if (g && n.startsWith(g)) n = n.slice(g.length);
  n = n.replace(/^(VCO \d|Filter [AB]|EG \d|LFO \d|Seq|Arp|Gen|FX \d|Effects|Mod \d+|Macro|OTA|Comb|Formant|VCA ?\d?|Noise|Ring Mod|S&H|Slew|Follower|Diode Ladder|Ladder|VC BPF)\s+/i, '');
  return n.trim().toLowerCase() || p.n.toLowerCase();
}

// ---------------------------------------------------------------------------
// Parameter writes and view binding
// ---------------------------------------------------------------------------
let saveTimer;
function setParam(a, v, { fromEngine = false, quiet = false } = {}) {
  const p = byAddr.get(a); if (!p) return;
  v = quant(p, v);
  values.set(a, v);
  if (!fromEngine) engine.post({ t: 'set', a, v });
  for (const f of views.get(a) || []) f(v);
  if (!quiet) { clearTimeout(saveTimer); saveTimer = setTimeout(saveSession, 600); }
  if (fxSlotOf(a)?.i === 0 && !fromEngine) {}  // type changes go through setEffectType
}
function bind(a, f) { if (!views.has(a)) views.set(a, []); views.get(a).push(f); f(values.get(a) ?? byAddr.get(a)?.d ?? 0); }

// Learn target selection: clicking a control while Learn is armed.
let learn = { armed: false, target: null };
function controlPicked(a, el) {
  if (!learn.armed) return false;
  $$('.learn-target').forEach((e) => e.classList.remove('learn-target'));
  learn.target = a; el.classList.add('learn-target');
  engine.post({ t: 'learn', on: true });
  toast(`Move a control on your MIDI device for “${byAddr.get(a).n}”`);
  renderLearnState();
  return true;
}

// Generic drag behaviour for faders, knobs and wheels.
function draggable(el, p, { range = 160, onStart } = {}) {
  let start = 0, n0 = 0, id = null;
  const a = p.a;
  el.tabIndex = 0;
  el.setAttribute('role', 'slider');
  el.setAttribute('aria-label', p.n);
  el.addEventListener('pointerdown', (e) => {
    if (controlPicked(a, el)) return;
    id = e.pointerId; el.setPointerCapture(id); el.classList.add('drag');
    start = e.clientY; n0 = norm(p, values.get(a) ?? p.d); onStart && onStart(); e.preventDefault();
  });
  el.addEventListener('pointermove', (e) => {
    if (e.pointerId !== id) return;
    const k = e.shiftKey ? 0.15 : 1;
    setParam(a, denorm(p, n0 + (start - e.clientY) / range * k));
  });
  const end = (e) => { if (e.pointerId === id) { id = null; el.classList.remove('drag'); } };
  el.addEventListener('pointerup', end); el.addEventListener('pointercancel', end);
  el.addEventListener('dblclick', () => setParam(a, p.d));
  el.addEventListener('wheel', (e) => { e.preventDefault(); setParam(a, denorm(p, norm(p, values.get(a) ?? p.d) - Math.sign(e.deltaY) * (e.shiftKey ? 0.005 : 0.02))); }, { passive: false });
  el.addEventListener('keydown', (e) => {
    const step = e.shiftKey ? 0.1 : 0.01, n = norm(p, values.get(a) ?? p.d);
    const d = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step, PageUp: 0.1, PageDown: -0.1 }[e.key];
    if (d !== undefined) { e.preventDefault(); setParam(a, denorm(p, n + d)); }
    if (e.key === 'Home') setParam(a, p.lo); if (e.key === 'End') setParam(a, p.hi);
  });
}
function ariaValue(el, p, v) { el.setAttribute('aria-valuenow', String(v)); el.setAttribute('aria-valuemin', String(p.lo)); el.setAttribute('aria-valuemax', String(p.hi)); el.setAttribute('aria-valuetext', fmt(p, v)); }

function faderEl(id, label) {
  const p = P[id]; if (!p) return '';
  return `<div class="ctl" data-a="${p.a}" data-kind="fader"><div class="fader"><div class="scale"></div><div class="slot"></div><div class="capk"></div></div><span class="val"></span><div class="cap-l">${esc(label ?? legend(p))}</div></div>`;
}
function knobEl(id, label, big) {
  const p = P[id]; if (!p) return '';
  return `<div class="ctl" data-a="${p.a}" data-kind="knob"><div class="knob${big ? ' big' : ''}"><div class="ticks"></div><div class="skirt"></div><div class="capk"></div></div><span class="val"></span><div class="cap-l">${esc(label ?? legend(p))}</div></div>`;
}
function switchEl(id, label) {
  const p = P[id]; if (!p) return '';
  const many = p.s && p.s.length > 4;
  if (many) return `<div class="ctl sw" data-a="${p.a}" data-kind="rotary"><div class="knob rotary"><div class="ticks"></div><div class="skirt"></div><div class="capk"></div></div><div class="rotary-l"></div><div class="cap-l">${esc(label ?? legend(p))}</div></div>`;
  const pos = p.s || ['off', 'on'];
  return `<div class="ctl sw" data-a="${p.a}" data-kind="lever"><div class="pos">${pos.map((s, i) => `<b data-i="${i}">${esc(String(s).toLowerCase())}</b>`).join('')}</div><div class="lever"><i></i></div><div class="cap-l">${esc(label ?? legend(p))}</div></div>`;
}
// Wires every control element inside `root` to its parameter.
function wire(root) {
  for (const el of $$('.ctl[data-a]', root)) {
    if (el.dataset.wired) continue; el.dataset.wired = '1';
    const a = +el.dataset.a, p = byAddr.get(a), kind = el.dataset.kind, val = $('.val', el);
    if (kind === 'fader' || kind === 'knob') {
      const body = el.firstElementChild;
      draggable(body, p, { range: kind === 'fader' ? 150 : 200 });
      bind(a, (v) => { body.style.setProperty('--n', clamp(norm(p, v), 0, 1)); if (val) val.textContent = fmt(p, v); ariaValue(body, p, v); });
    } else if (kind === 'rotary') {
      const body = el.firstElementChild, lab = $('.rotary-l', el);
      draggable(body, p, { range: 260 });
      body.addEventListener('click', (e) => { if (learn.armed) return; if (e.detail === 1 && !body.classList.contains('drag')) {} });
      bind(a, (v) => { body.style.setProperty('--n', clamp(norm(p, v), 0, 1)); lab.textContent = fmt(p, v); ariaValue(body, p, v); });
    } else if (kind === 'lever') {
      const n = (p.s ? p.s.length : 2), lever = $('.lever', el), bs = $$('.pos b', el);
      el.tabIndex = 0; el.setAttribute('role', 'slider'); el.setAttribute('aria-label', p.n);
      const next = (d) => { const v = Math.round(values.get(a) ?? p.d); setParam(a, p.lo + (((v - p.lo + d) % n) + n) % n); };
      el.addEventListener('click', (e) => { if (controlPicked(a, el)) return; const b = e.target.closest('b[data-i]'); if (b) setParam(a, p.lo + +b.dataset.i); else next(1); });
      el.addEventListener('keydown', (e) => { if (['ArrowUp', 'ArrowRight', ' ', 'Enter'].includes(e.key)) { e.preventDefault(); next(1); } if (['ArrowDown', 'ArrowLeft'].includes(e.key)) { e.preventDefault(); next(-1); } });
      bind(a, (v) => {
        const i = Math.round(v - p.lo);
        lever.style.setProperty('--a', `${n === 1 ? 0 : -40 + 80 * i / (n - 1)}deg`);
        bs.forEach((b, k) => b.classList.toggle('on', k === i)); ariaValue(el, p, v);
      });
    }
  }
}

// ---------------------------------------------------------------------------
// Panels (Phase 8 panel language)
// ---------------------------------------------------------------------------
function rnd(seed) { let s = seed >>> 0 || 1; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296); }
function hash(str) { let h = 2166136261; for (const c of str) h = Math.imul(h ^ c.charCodeAt(0), 16777619); return h >>> 0; }
// Procedural, seeded wear: hairline scratches, edge rub, smudges. Stable per module.
function wearSVG(key, w, h) {
  const r = rnd(hash(key)), parts = [];
  for (let i = 0; i < 26; i++) {
    const x = r() * w, y = h * (0.45 + 0.55 * r()), len = 6 + r() * 26, ang = (r() - 0.5) * 1.2;
    parts.push(`<path d="M${x.toFixed(1)} ${y.toFixed(1)}l${(Math.cos(ang) * len).toFixed(1)} ${(Math.sin(ang) * len).toFixed(1)}" stroke="#cfcfcf" stroke-width="${(0.3 + r() * 0.5).toFixed(2)}" opacity="${(0.25 + r() * 0.45).toFixed(2)}"/>`);
  }
  for (let i = 0; i < 10; i++) { const y = r() * h, side = r() < 0.5 ? 0 : w; parts.push(`<ellipse cx="${side}" cy="${y.toFixed(0)}" rx="${(1 + r() * 2.5).toFixed(1)}" ry="${(3 + r() * 10).toFixed(1)}" fill="#8a8a8a" opacity=".5"/>`); }
  for (let i = 0; i < 4; i++) parts.push(`<circle cx="${(r() * w).toFixed(0)}" cy="${(r() * h).toFixed(0)}" r="${(8 + r() * 14).toFixed(0)}" fill="#fff" opacity=".035"/>`);
  return `<svg class="wear" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none" aria-hidden="true">${parts.join('')}</svg>`;
}
// Block diagrams: thin white boxes and arrows, our own drawing.
function diagram(kind) {
  const box = (x, t, w = 26) => `<rect x="${x}" y="6" width="${w}" height="20" rx="1.5" fill="none" stroke="currentColor" stroke-width="1"/><text x="${x + w / 2}" y="20" text-anchor="middle" font-size="9" fill="currentColor">${t}</text>`;
  const arr = (x1, x2) => `<path d="M${x1} 16H${x2 - 3}" stroke="currentColor" stroke-width="1"/><path d="M${x2 - 4} 13l4 3-4 3z" fill="currentColor"/>`;
  const sh = {
    osc: arr(2, 18) + box(18, '∿') + arr(44, 58) + box(58, 'wave', 30) + arr(88, 100),
    lfo: box(10, '∿') + arr(36, 52) + box(52, 'lvl', 26) + arr(78, 96),
    env: `<path d="M8 28 L26 6 L42 16 H70 L92 28" fill="none" stroke="currentColor" stroke-width="1"/><text x="26" y="34" font-size="7" fill="currentColor">a d s r</text>`,
    filter: arr(2, 16) + box(16, 'vcf', 30) + arr(46, 60) + `<path d="M62 10 H78 Q86 10 90 26" fill="none" stroke="currentColor" stroke-width="1"/>` + arr(90, 100),
    ladder: arr(2, 14) + box(14, '4p', 22) + box(38, '▸', 18) + arr(56, 70) + `<path d="M70 10 H80 Q88 10 94 26" fill="none" stroke="currentColor"/>`,
    amp: arr(2, 26) + `<path d="M28 6 L28 26 L56 16 Z" fill="none" stroke="currentColor"/>` + arr(56, 96),
    noise: `<path d="M8 16 l4 -6 l3 10 l4 -12 l3 14 l4 -8 l3 6 l4 -9 l3 11 l4 -7" fill="none" stroke="currentColor"/>` + arr(50, 96),
    ring: arr(2, 30) + `<circle cx="42" cy="16" r="10" fill="none" stroke="currentColor"/><path d="M35 9 L49 23 M49 9 L35 23" stroke="currentColor"/>` + arr(52, 96),
    mixer: `<circle cx="50" cy="16" r="11" fill="none" stroke="currentColor"/><text x="50" y="20" text-anchor="middle" font-size="12" fill="currentColor">Σ</text>` + arr(4, 39) + arr(61, 96),
    sh: `<path d="M8 26 H22 V12 H36 V20 H50 V8 H64 V24 H78" fill="none" stroke="currentColor"/>` + arr(80, 98),
    macro: box(8, 'm1', 20) + box(31, 'm2', 20) + box(54, 'm3', 20) + box(77, 'm4', 20),
    keys: `<path d="M8 8 h80 v18 h-80 z M18 8v18 M28 8v18 M38 8v18 M48 8v18 M58 8v18 M68 8v18 M78 8v18" fill="none" stroke="currentColor"/>`,
    comb: arr(2, 18) + box(18, 'z⁻ⁿ', 30) + `<path d="M48 16 H58 V28 H33 V26" fill="none" stroke="currentColor"/>` + arr(58, 96),
    formant: arr(2, 14) + box(14, 'a', 18) + box(34, 'e', 18) + box(54, 'o', 18) + arr(72, 96),
    bank: [...Array(8)].map((_, i) => `<rect x="${6 + i * 11}" y="${8 + (i % 3) * 3}" width="8" height="${18 - (i % 3) * 3}" fill="none" stroke="currentColor"/>`).join(''),
    util: arr(2, 30) + box(30, 'f(x)', 36) + arr(66, 96),
    switch: arr(2, 22) + `<circle cx="28" cy="16" r="2" fill="currentColor"/><path d="M28 16 L52 8" stroke="currentColor"/><circle cx="56" cy="8" r="2" fill="currentColor"/><circle cx="56" cy="24" r="2" fill="currentColor"/>` + arr(58, 96),
    mult: arr(2, 40) + `<path d="M42 16 L72 6 M42 16 L72 16 M42 16 L72 26" stroke="currentColor"/>`,
    out: arr(2, 30) + `<path d="M34 11 h8 l10 -6 v22 l-10 -6 h-8 z" fill="none" stroke="currentColor"/><path d="M60 10 q6 6 0 12 M66 6 q10 10 0 20" fill="none" stroke="currentColor"/>`,
  };
  return `<svg class="diagram" viewBox="0 0 100 34" preserveAspectRatio="xMidYMid meet" style="color:var(--legend-ink)" aria-hidden="true">${sh[kind] || sh.util}</svg>`;
}
function jackEl(modIndex, port, input) {
  return `<div class="jack ${input ? 'in' : 'out'}" data-m="${modIndex}" data-p="${port.index}" data-in="${input ? 1 : 0}" role="button" tabindex="0" aria-label="${esc(rack[modIndex].name)} ${esc(port.name)} ${input ? 'input' : 'output'}"><div class="hole"></div><span class="jl">${esc(port.key.replace(/([A-Z])/g, ' $1').toLowerCase())}</span></div>`;
}
function panelEl(m) {
  const w = m.hp, mi = rack.findIndex((r) => r.key === m.rack);
  const jacks = mi >= 0 ? [...rack[mi].ins.map((p) => jackEl(mi, p, true)), ...rack[mi].outs.map((p) => jackEl(mi, p, false))].join('') : '';
  const led = m.key.startsWith('lfo') ? `<div class="led" data-led="${m.key}"></div>` : m.key.startsWith('eg') || m.key === 'keyboard' ? `<div class="led g" data-led="gate"></div>` : '';
  return `<article class="panel" style="--w:${w}" data-key="${m.key}" aria-label="${esc(m.title)}">
    <i class="screw s1" style="--r:${hash(m.key) % 180}deg"></i><i class="screw s2" style="--r:${hash(m.key + 'b') % 180}deg"></i><i class="screw s3"></i><i class="screw s4" style="--r:70deg"></i>
    <div class="ptitle">${esc(m.title)}</div>${diagram(m.diagram)}${led}
    ${m.sliders.length ? `<div class="bank">${m.sliders.map((id) => faderEl(id)).join('')}</div>` : ''}
    ${m.popup ? `<button class="tbtn popbtn" data-pop="${m.key}" aria-haspopup="dialog" aria-label="${esc(m.title)} settings">settings ▸</button>` : `
    ${m.knobs.length ? `<div class="knobs">${m.knobs.map((id) => knobEl(id)).join('')}</div>` : ''}
    ${m.switches.length ? `<div class="switches">${m.switches.map((id) => switchEl(id)).join('')}</div>` : ''}`}
    ${jacks ? `<div class="jacks">${jacks}</div>` : ''}
    ${m.popup ? `<dialog class="pop" id="pop-${m.key}" aria-label="${esc(m.title)}"><div class="poppanel">
      <div class="ptitle">${esc(m.title)}</div>
      ${m.knobs.length ? `<div class="knobs">${m.knobs.map((id) => knobEl(id)).join('')}</div>` : ''}
      ${m.switches.length ? `<div class="switches">${m.switches.map((id) => switchEl(id)).join('')}</div>` : ''}
      <button class="tbtn popclose" aria-label="Close">done</button></div></dialog>` : ''}
    ${wearSVG(m.key, w * 22, 430)}
  </article>`;
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
function cabinet(title, inner, id) { return `<div class="cab"${id ? ` id="${id}"` : ''}><h2>${esc(title)}</h2><div class="scroller"><div class="rack"><div class="dust" style="top:0"></div>${inner}<div class="dust" style="bottom:0"></div></div></div></div>`; }

// Panels with popup:true keep only their faders on the front; the rest opens in a dialog.
document.addEventListener('click', (e) => {
  const b = e.target.closest('.popbtn'); if (b) { $('#pop-' + b.dataset.pop)?.showModal(); return; }
  const d = e.target.closest('dialog.pop'); if (!d) return;
  if (e.target.closest('.popclose') || e.target === d) d.close();   // done button, or a tap outside the panel
});
function buildRack() {
  const s = $('#s-rack');
  s.innerHTML = `<div id="rackWrap" style="position:relative">${panels.rows.map((r) => cabinet(r.name, r.modules.map(panelEl).join(''))).join('')}<svg class="cables" id="cables"></svg></div>
    <div class="sheet"><div class="row"><span class="note">Patch: click an output jack (white label) and then an input jack. Click a cable to remove it. Cables replace the default (normalled) connection of the input they plug into.</span>
    <label class="tfield">cable opacity <input type="range" id="cableOpacity" min="0.2" max="1" step="0.05" value="${store.get('cableOpacity', 0.85)}"></label>
    <button class="tbtn" id="clearCables">remove all cables</button></div></div>`;
  wire(s);
  $('#cableOpacity').addEventListener('input', (e) => { store.set('cableOpacity', +e.target.value); drawCables(); });
  $('#clearCables').addEventListener('click', () => { cables = []; publishPatch(); });
  s.addEventListener('click', jackClick);
  setupPatchGestures(s);
  s.addEventListener('keydown', (e) => { if ((e.key === 'Enter' || e.key === ' ') && e.target.closest('.jack')) { e.preventDefault(); jackClick(e); } });
  for (const sc of $$('.scroller', s)) sc.addEventListener('scroll', drawCables, { passive: true });
}

function buildSequencer() {
  const step = (k, f) => `seq.step${k}${f}`;
  const cols = [...Array(16)].map((_, i) => {
    const k = i + 1;
    return `<div style="display:flex;flex-direction:column;align-items:center;gap:8px;width:66px">
      <div class="led" data-step="${i}"></div>${faderEl(step(k, 'A'), `${k}`)}${knobEl(step(k, 'B'), 'b')}${knobEl(step(k, 'C'), 'c')}
      ${switchEl(step(k, 'Gate'), 'gate')}${knobEl(step(k, 'Probability'), 'prob')}${switchEl(step(k, 'Ratchet'), 'ratchet')}</div>`;
  }).join('');
  const settings = ['seq.run', 'seq.length', 'seq.direction', 'seq.rate', 'seq.pitchRange', 'seq.scale', 'seq.root'].map((id) => switchEl(id)).join('')
    + ['seq.swing', 'seq.gate', 'seq.transpose', 'seq.seed'].map((id) => knobEl(id)).join('') + switchEl('seq.playNotes') + switchEl('seq.rowBVelocity');
  $('#s-sequencer').innerHTML = cabinet('analogue sequencer · 16 steps × 3 rows',
    `<article class="panel" style="--w:${16 * 3 + 2};min-height:0"><div class="ptitle">sequencer</div>${diagram('sh')}
     <div class="switches" style="justify-content:flex-start;gap:14px 18px">${settings}</div><div style="display:flex;gap:0;justify-content:space-between">${cols}</div>${wearSVG('seq', 1100, 640)}</article>`)
    + `<div class="sheet"><p>Row A is pitch (quantised to the scale), B velocity, C a modulation source (matrix "Seq Row C"). Set Run to Run, Transport (follows Play) or Key (plays while a key is held; keys transpose).</p></div>`;
  wire($('#s-sequencer'));
}

function groupPanel(title, prefix, exclude = [], width) {
  const ps = Object.values(P).filter((p) => p.g === prefix && !exclude.some((x) => p.id.includes(x)));
  const sw = ps.filter((p) => p.s || p.u === 'boolean'), kn = ps.filter((p) => !(p.s || p.u === 'boolean'));
  return `<article class="panel" style="--w:${width || Math.max(10, Math.ceil(ps.length / 2) * 3)};min-height:0"><div class="ptitle">${esc(title)}</div>
    <div class="led" data-led="${prefix}"></div>
    <div class="switches">${sw.map((p) => switchEl(p.id)).join('')}</div><div class="knobs" style="margin-top:10px">${kn.map((p) => knobEl(p.id)).join('')}</div>${wearSVG(prefix, 500, 520)}</article>`;
}
function buildArp() {
  $('#s-arp').innerHTML = cabinet('arpeggiator · generative engine', groupPanel('arpeggiator', 'arp', [], 26) + groupPanel('generative', 'gen', [], 22))
    + `<div class="sheet"><h3>preset creator</h3><p>Generates a playable patch in a category from a seed, the same as the app's creator. Hold a chord with the arpeggiator on, or turn the generative engine to Run, then press Play.</p>
      <div class="row" id="genRow"></div><p class="note" id="genBar"></p></div>`;
  wire($('#s-arp'));
  const cats = ['bass', 'lead', 'pad', 'pluck', 'keys', 'brass', 'fx', 'drone'];
  $('#genRow').innerHTML = `<label class="tfield">seed <input type="number" id="seed" min="1" max="99999" value="${store.get('seed', 7)}"></label>` + cats.map((c, i) => `<button class="tbtn" data-cat="${i}">${c}</button>`).join('');
  $('#genRow').addEventListener('click', (e) => { const b = e.target.closest('[data-cat]'); if (!b) return; generatePreset(+$('#seed').value || 1, +b.dataset.cat, b.textContent); });
}

let fxTypes = [];
function buildEffects() {
  const slot = (s) => {
    const b = `fx${s + 1}`;
    return `<article class="panel fxslot" style="--w:13;min-height:0" data-slot="${s}"><div class="ptitle">fx ${s + 1}</div>
      <div class="row" style="justify-content:center"><select class="fxtype" data-slot="${s}" aria-label="Effect slot ${s + 1} type">${fxTypes.map((t, i) => `<option value="${i}">${esc(i ? `${t.family} · ${t.name}` : 'empty')}</option>`).join('')}</select><div class="led g" data-fxled="${s}"></div></div>
      <div class="switches">${switchEl(`${b}.enabled`, 'on')}${switchEl(`${b}.bus`, 'bus')}</div>
      <div class="bank" style="min-height:0">${faderEl(`${b}.mix`, 'mix')}</div>
      <div class="knobs">${knobEl(`${b}.output`, 'out')}${[...Array(12)].map((_, k) => `<div class="fxp" data-k="${k}">${knobEl(`${b}.p${k + 1}`, '·')}</div>`).join('')}</div>${wearSVG('fx' + s, 290, 700)}</article>`;
  };
  $('#s-effects').innerHTML = cabinet('effects · 8 slots, insert and two sends', [...Array(8)].map((_, s) => slot(s)).join(''))
    + cabinet('effects buses', groupPanel('buses', 'fxBus', [], 26)) + `<div class="sheet"><p class="note" id="fxLatency"></p></div>`;
  wire($('#s-effects'));
  for (const sel of $$('.fxtype')) {
    const s = +sel.dataset.slot;
    sel.addEventListener('change', () => setEffectType(s, +sel.value));
    bind((39 + s) * 256, (v) => { sel.value = String(Math.round(v)); relabelSlot(s); });
  }
}
function relabelSlot(s) {
  const type = Math.round(values.get((39 + s) * 256) || 0), el = $(`.fxslot[data-slot="${s}"]`); if (!el) return;
  for (const d of $$('.fxp', el)) {
    const k = +d.dataset.k, name = cstr(ui.vkw_fx_param_name(type, k));
    d.style.display = name ? '' : 'none';
    $('.cap-l', d).textContent = name.toLowerCase();
    const a = (39 + s) * 256 + 5 + k; for (const f of views.get(a) || []) f(values.get(a) ?? 0.5);
  }
}
function setEffectType(s, type) {
  engine.post({ t: 'fxtype', slot: s, type });
  values.set((39 + s) * 256, type);
  relabelSlot(s);
}

function buildMatrix() {
  const srcs = P['matrix.slot1Source'].s;
  const dests = [['0', 'none'], ...Object.entries(virtualDests).map(([k, v]) => [String(v), `◆ ${k.replace('cutoffAll', 'all cutoffs')}`]),
    ...Object.values(P).filter((p) => p.mod).map((p) => [String(p.a), p.n])];
  const rows = [...Array(16)].map((_, i) => {
    const b = `matrix.slot${i + 1}`;
    return `<tr><td>${i + 1}</td><td>${switchEl(b + 'Enable', '')}</td><td>${switchEl(b + 'Source', '')}</td><td>${switchEl(b + 'Via', '')}</td>
      <td><select class="dest" data-a="${P[b + 'Dest'].a}" aria-label="Slot ${i + 1} destination">${dests.map(([v, n]) => `<option value="${v}">${esc(n)}</option>`).join('')}</select></td>
      <td>${knobEl(b + 'Amount', '')}</td><td>${knobEl(b + 'Offset', '')}</td><td>${switchEl(b + 'Curve', '')}</td><td>${switchEl(b + 'Polarity', '')}</td><td>${switchEl(b + 'Invert', '')}</td><td>${knobEl(b + 'Slew', '')}</td>
      <td><div class="bar"><i data-mod="${i}" style="width:0"></i></div></td></tr>`;
  }).join('');
  $('#s-matrix').innerHTML = `<div class="sheet"><h3>modulation matrix</h3><p>${srcs.length - 1} sources onto any continuous parameter, per voice. Amounts move parameters along their own taper, so a cutoff moves in octaves.</p>
    <div class="scroll"><table class="matrix"><thead><tr><th>#</th><th>on</th><th>source</th><th>scale by</th><th>destination</th><th>amount</th><th>offset</th><th>curve</th><th>polarity</th><th>invert</th><th>slew</th><th>live</th></tr></thead><tbody>${rows}</tbody></table></div>
    <h3 style="margin-top:16px">macros</h3><div class="row">${[1, 2, 3, 4].map((k) => knobEl(`macro.macro${k}`, `macro ${k}`, true)).join('')}</div></div>`;
  wire($('#s-matrix'));
  for (const sel of $$('select.dest')) {
    const a = +sel.dataset.a;
    sel.addEventListener('change', () => setParam(a, +sel.value));
    bind(a, (v) => { sel.value = String(Math.round(v)); });
  }
}

// MIDI page ------------------------------------------------------------------
let mappings = store.get('mappings', []);   // profile-format mappings (identifiers)
let midiAccess = null, midiOut = null;
const SRC = ['cc', 'cc14', 'nrpn', 'pitchBend', 'channelPressure', 'note'], MODES = ['absolute', 'toggle', 'relative'], RELS = ['twosComplement', 'binaryOffset', 'signedBit'];
function buildMidi() {
  const settings = ['global.syncSource', 'global.receiveChannel', 'global.velocityCurve', 'global.programChange', 'global.midiOut', 'global.midiOutChannel', 'global.midiClockOut', 'global.mpeMode', 'global.mpeMemberChannels', 'global.zoneMode', 'global.splitPoint']
    .map((id) => switchEl(id)).join('') + ['global.pitchBendRange', 'global.bendDown', 'global.mpePitchRange', 'global.zoneATranspose', 'global.zoneBTranspose'].map((id) => knobEl(id)).join('');
  $('#s-midi').innerHTML = cabinet('midi', `<article class="panel" style="--w:40;min-height:0"><div class="ptitle">midi interface</div><div class="switches" style="gap:14px 20px">${settings}</div>${wearSVG('midi', 900, 300)}</article>`)
    + `<div class="sheet"><h3>devices</h3><div class="row"><button class="tbtn" id="midiConnect">connect midi devices</button>
       <label class="tfield">input <select id="midiInSel"><option value="all">all inputs</option></select></label>
       <label class="tfield">output <select id="midiOutSel"><option value="">none</option></select></label></div>
       <p class="note" id="midiNote">Web MIDI needs Chrome or Edge (or Safari 18+), and your permission. Without it, play from the on-screen or computer keyboard.</p>
       <h3 style="margin-top:14px">monitor</h3><div class="row"><span class="readout" id="monLast" style="min-width:220px">—</span><span class="readout" id="monCount">in 0 · out 0</span><span class="readout" id="monSync">internal</span></div></div>
     <div class="sheet"><h3>midi learn</h3><p>Press Learn, click the control to map, then move a knob, fader, key or encoder on your controller. Mappings are kept in this browser and export as a .vekturmidi profile the app imports.</p>
       <div class="row"><button class="tbtn" id="learnBtn" aria-pressed="false"><span class="lamp"></span>learn</button>
         <label class="tfield">mode <select id="learnMode">${MODES.map((m) => `<option>${m}</option>`).join('')}</select></label>
         <label class="tfield">encoder <select id="learnRel">${RELS.map((m) => `<option>${m}</option>`).join('')}</select></label>
         <label class="tfield"><input type="checkbox" id="learnPickup"> pickup</label>
         <button class="tbtn" id="stdProfile">load standard layout</button><span class="note" id="learnState"></span></div>
       <div class="scroll"><table class="matrix" id="mapTable"></table></div>
       <h3 style="margin-top:14px">profile</h3><div class="row"><button class="tbtn" id="copyProfile">copy profile json</button><button class="tbtn" id="loadProfile">import pasted json</button></div>
       <textarea id="profileText" aria-label="Profile JSON" placeholder="Paste a .vekturmidi profile here, then Import"></textarea></div>`;
  wire($('#s-midi'));
  $('#midiConnect').addEventListener('click', connectMidi);
  $('#midiInSel').addEventListener('change', attachInputs);
  $('#midiOutSel').addEventListener('change', () => { midiOut = midiAccess && $('#midiOutSel').value ? midiAccess.outputs.get($('#midiOutSel').value) : null; });
  $('#learnBtn').addEventListener('click', () => { learn.armed = !learn.armed; if (!learn.armed) { learn.target = null; engine.post({ t: 'learn', on: false }); $$('.learn-target').forEach((e) => e.classList.remove('learn-target')); } renderLearnState(); if (learn.armed) toast('Learn: click the control to map'); });
  $('#stdProfile').addEventListener('click', () => { for (const m of STANDARD) assignMapping({ ...m }); toast('Standard layout added'); });
  $('#copyProfile').addEventListener('click', async () => { const txt = JSON.stringify(profile(), null, 1); $('#profileText').value = txt; try { await navigator.clipboard.writeText(txt); toast('Profile copied'); } catch { toast('Profile is in the text box'); } });
  $('#loadProfile').addEventListener('click', () => {
    try { const j = JSON.parse($('#profileText').value); if (j.format !== 'com.dmtcymatics.vekturmodular.midi-profile') throw 0; if ((j.version || 1) > 1) { toast('That profile needs a newer version'); return; }
      let n = 0; for (const m of j.mappings || []) if (P[m.parameter] && SRC.includes(m.source)) { assignMapping(m); n++; } toast(`Imported ${n} mappings`); }
    catch { toast('Not a Vektur MIDI profile'); }
  });
  renderMappings();
}
const STANDARD = [[74, 'vcfB.cutoff'], [71, 'vcfB.resonance'], [73, 'eg2.attack'], [75, 'eg2.decay'], [72, 'eg2.release'], [76, 'lfo1.rate'], [5, 'global.glide'], [7, 'global.masterVolume', true]]
  .map(([n, p, pk]) => ({ source: 'cc', number: n, parameter: p, minimum: 0, maximum: 1, mode: 'absolute', relative: 'twosComplement', pickup: !!pk, sensitivity: 1 / 128 }));
function profile() { return { format: 'com.dmtcymatics.vekturmodular.midi-profile', version: 1, name: 'Web test mappings', mappings }; }
function assignMapping(m) { mappings = mappings.filter((x) => x.parameter !== m.parameter); mappings.push(m); store.set('mappings', mappings); publishMappings(); renderMappings(); }
function publishMappings() {
  engine.post({ t: 'maps', list: mappings.filter((m) => P[m.parameter]).map((m) => ({ source: SRC.indexOf(m.source), channel: m.channel == null ? 255 : m.channel, number: m.number || 0,
    address: P[m.parameter].a, min: m.minimum ?? 0, max: m.maximum ?? 1, mode: MODES.indexOf(m.mode || 'absolute'), relative: Math.max(0, RELS.indexOf(m.relative)), pickup: !!m.pickup, sens: m.sensitivity || 1 / 128 })) });
  $$('.ctl.mapped').forEach((e) => e.classList.remove('mapped'));
  for (const m of mappings) if (P[m.parameter]) $$(`.ctl[data-a="${P[m.parameter].a}"]`).forEach((e) => e.classList.add('mapped'));
}
function srcText(m) { const ch = m.channel == null ? 'any' : `ch ${m.channel + 1}`; return { cc: `CC ${m.number}`, cc14: `CC ${m.number}/${m.number + 32}`, nrpn: `NRPN ${m.number >> 7}:${m.number & 127}`, pitchBend: 'bend', channelPressure: 'pressure', note: `note ${m.number}` }[m.source] + ' · ' + ch; }
function renderMappings() {
  const t = $('#mapTable'); if (!t) return;
  t.innerHTML = `<thead><tr><th>control</th><th>parameter</th><th>mode</th><th>range</th><th></th></tr></thead><tbody>${mappings.map((m, i) => `<tr><td>${esc(srcText(m))}</td><td>${esc(P[m.parameter]?.n || m.parameter)}</td><td>${esc(m.mode)}${m.pickup ? ' · pickup' : ''}</td>
    <td>${Math.round((m.minimum ?? 0) * 100)}–${Math.round((m.maximum ?? 1) * 100)} %</td><td><button class="tbtn" data-rm="${i}" aria-label="Remove mapping">remove</button></td></tr>`).join('') || '<tr><td colspan="5" class="note">No mappings yet.</td></tr>'}</tbody>`;
  t.onclick = (e) => { const b = e.target.closest('[data-rm]'); if (b) { mappings.splice(+b.dataset.rm, 1); store.set('mappings', mappings); publishMappings(); renderMappings(); } };
}
function renderLearnState() {
  const b = $('#learnBtn'); if (!b) return; b.setAttribute('aria-pressed', learn.armed);
  $('#learnState').textContent = learn.armed ? (learn.target != null ? `waiting for a control → ${byAddr.get(learn.target).n}` : 'click a control to map') : '';
}
function learned(e) {
  if (learn.target == null) return;
  const p = byAddr.get(learn.target), src = SRC[e.source];
  let mode = $('#learnMode').value; if (src === 'note' && mode === 'absolute') mode = 'toggle';
  assignMapping({ source: src, number: e.number, parameter: p.id, minimum: 0, maximum: 1, mode, relative: $('#learnRel').value, pickup: $('#learnPickup').checked, sensitivity: 1 / 128 });
  toast(`${srcText({ source: src, number: e.number })} → ${p.n}`);
  learn = { armed: false, target: null }; $$('.learn-target').forEach((x) => x.classList.remove('learn-target')); renderLearnState();
}
async function connectMidi() {
  if (!navigator.requestMIDIAccess) { $('#midiNote').textContent = 'This browser has no Web MIDI. Use Chrome or Edge, or the app.'; return; }
  try {
    midiAccess = await navigator.requestMIDIAccess({ sysex: false });
    const fill = () => {
      $('#midiInSel').innerHTML = '<option value="all">all inputs</option>' + [...midiAccess.inputs.values()].map((i) => `<option value="${esc(i.id)}">${esc(i.name)}</option>`).join('');
      $('#midiOutSel').innerHTML = '<option value="">none</option>' + [...midiAccess.outputs.values()].map((o) => `<option value="${esc(o.id)}">${esc(o.name)}</option>`).join('');
      attachInputs();
      $('#midiNote').textContent = `${midiAccess.inputs.size} input(s), ${midiAccess.outputs.size} output(s).`;
    };
    midiAccess.onstatechange = fill; fill();
  } catch (e) { $('#midiNote').textContent = 'MIDI access was refused here (the page may be embedded). Open the page on its own, or use the keyboard.'; }
}
function attachInputs() {
  if (!midiAccess) return;
  const sel = $('#midiInSel').value;
  for (const i of midiAccess.inputs.values()) i.onmidimessage = sel === 'all' || sel === i.id ? (ev) => midiIn(Array.from(ev.data)) : null;
}
function midiIn(d) {
  if (!d.length || d[0] === 0xF0) return;
  engine.post({ t: 'midi', d });
  if ((d[0] & 0xF0) === 0x90 && d[2] > 0) keyLight(d[1], true); else if ((d[0] & 0xF0) === 0x80 || ((d[0] & 0xF0) === 0x90 && d[2] === 0)) keyLight(d[1], false);
}

// Presets --------------------------------------------------------------------
function loadFactory(p) {
  for (const q of Object.values(P)) if (!/^global\.(sync|receive|velocityCurve|programChange|midi|mpe|zone|split|tempo)/.test(q.id)) setParam(q.a, q.d, { quiet: true });
  for (const slot of Object.keys(p.fxTypes || {})) engine.post({ t: 'fxtype', slot: +slot, type: p.fxTypes[slot] });
  for (const [id, v] of Object.entries(p.parameters)) if (P[id]) setParam(P[id].a, v, { quiet: true });
  const key = (ref) => { const [m, port] = ref.split('.'); const mi = rack.findIndex((r) => r.key === m); return mi < 0 ? null : { m: mi, ins: rack[mi].ins, outs: rack[mi].outs, port }; };
  cables = [];
  p.cables.forEach((c, i) => {
    const a = key(c.from), b = key(c.to); if (!a || !b) return;
    const sp = a.outs.find((x) => x.key === a.port), dp = b.ins.find((x) => x.key === b.port); if (!sp || !dp) return;
    cables.push([i, a.m, sp.index, b.m, dp.index, c.gain, i]); springs.set(i, { sag: 0, vel: 600 });
  });
  publishPatch();
  for (let s = 0; s < 8; s++) relabelSlot(s);
  toast(`${p.name} — ${factory.banks[p.bank]} ${p.number + 1}`);
}
function buildPresets() {
  const fac = factory ? factory.banks.map((bank, b) => `<h3 style="margin-top:14px">${esc(bank.toLowerCase())}</h3><div class="grid" style="grid-template-columns:repeat(auto-fill,minmax(180px,1fr))">${
    factory.presets.filter((p) => p.bank === b).map((p) => `<button class="tbtn" data-fp="${factory.presets.indexOf(p)}" title="${esc(p.category)}" style="justify-content:space-between"><span>${esc(p.name)}</span><span class="mini">${esc(p.category.toLowerCase())}</span></button>`).join('')}</div>`).join('') : '';
  $('#s-presets').innerHTML = `<div class="sheet"><h3>factory presets</h3><p>Two banks of 32, made with every part of the instrument. Each is gain-staged and checked to sound distinct.</p>${fac}</div>
    <div class="sheet"><h3>your presets</h3><p>Saved in this browser. "Copy state" gives JSON in the app's state format (parameters by identifier), which the app can load.</p>
    <div class="row"><input type="text" id="presetName" placeholder="Preset name" aria-label="Preset name" style="min-width:220px"><button class="tbtn" id="presetSave">save</button><button class="tbtn" id="presetInit">initialise</button>
    <button class="tbtn" id="stateCopy">copy state</button><button class="tbtn" id="stateLoad">load pasted state</button></div>
    <div class="scroll"><table class="matrix" id="presetTable"></table></div><textarea id="stateText" aria-label="State JSON" placeholder="Paste state JSON here, then Load"></textarea></div>`;
  $('#s-presets').addEventListener('click', (e) => { const b = e.target.closest('[data-fp]'); if (b) loadFactory(factory.presets[+b.dataset.fp]); });
  $('#presetSave').addEventListener('click', () => { const n = $('#presetName').value.trim() || `Preset ${new Date().toLocaleString()}`; const all = store.get('presets', {}); all[n] = snapshot(); store.set('presets', all); renderPresets(); toast(`Saved “${n}”`); });
  $('#presetInit').addEventListener('click', () => { applyState({ parameters: Object.fromEntries(Object.values(P).map((p) => [p.id, p.d])), cables: [] }); toast('Initialised'); });
  $('#stateCopy').addEventListener('click', async () => { const t = JSON.stringify(snapshot()); $('#stateText').value = t; try { await navigator.clipboard.writeText(t); toast('State copied'); } catch { toast('State is in the text box'); } });
  $('#stateLoad').addEventListener('click', () => { try { applyState(JSON.parse($('#stateText').value)); toast('State loaded'); } catch { toast('That is not valid JSON'); } });
  renderPresets();
}
function renderPresets() {
  const all = store.get('presets', {}), names = Object.keys(all).sort((a, b) => a.localeCompare(b));
  const t = $('#presetTable');
  t.innerHTML = `<thead><tr><th>name</th><th></th></tr></thead><tbody>${names.map((n) => `<tr><td>${esc(n)}</td><td><button class="tbtn" data-load="${esc(n)}">load</button> <button class="tbtn" data-del="${esc(n)}">delete</button></td></tr>`).join('') || '<tr><td colspan="2" class="note">No saved presets yet.</td></tr>'}</tbody>`;
  t.onclick = (e) => { const l = e.target.closest('[data-load]'), d = e.target.closest('[data-del]'); const all2 = store.get('presets', {});
    if (l) { applyState(all2[l.dataset.load]); toast(`Loaded “${l.dataset.load}”`); } if (d) { delete all2[d.dataset.del]; store.set('presets', all2); renderPresets(); } };
}
function snapshot() { return { schemaVersion: 1, writtenBy: 'web-test', parameters: Object.fromEntries(Object.values(P).map((p) => [p.id, values.get(p.a) ?? p.d])), cables, midiMappings: mappings }; }
function applyState(st) {
  if (!st || typeof st !== 'object') return;
  const params = st.parameters || {};
  for (const p of Object.values(P)) { const v = params[p.id]; if (typeof v === 'number' && Number.isFinite(v)) setParam(p.a, v, { quiet: true }); }
  for (let s = 0; s < 8; s++) relabelSlot(s);
  engine.post({ t: 'set', a: 39 * 256, v: values.get(39 * 256) }); engine.post({ t: 'values', addrs: [] });
  if (Array.isArray(st.cables)) { cables = st.cables.filter((c) => Array.isArray(c) && c.length === 6); publishPatch(); }
  saveSession();
}
function saveSession() { store.set('session', snapshot()); }
function generatePreset(seed, cat, name) { store.set('seed', seed); engine.post({ t: 'preset', seed, cat }); toast(`Generated ${name} #${seed}`); }

function buildAbout() {
  $('#s-about').innerHTML = `<div class="sheet"><h3>about this build</h3>
    <p>Vektur Modular web version, build 1.0.0. The engine is the instrument's C++ kernel compiled to WebAssembly with clang; it renders in an AudioWorklet at your sound card's rate. Its output matches the native build to better than −160 dB on the reference render.</p>
    <p>What differs from the app: no AUv3 host, no Ableton Link, no impulse-response files or MIDI-clip export, and user presets live in this browser. Preset, project and pattern files use the app's format, so they open in the app.</p>
    <p>Panels follow the Phase 8 hardware language (docs/design/HARDWARE_AESTHETIC.md): painted black panels, white silkscreen, slider banks, silver knobs, brushed rails and seeded wear. Every legend and layout is original.</p>
    <img src="logo-256.png" width="160" height="160" alt="DMT Cymatics logo" style="display:block;margin:10px 0">
    <p class="note">DMT Cymatics — Dream Music Temple · DreamMusicTemple.com · DMTcymatics@humanoid.net</p></div>`;
}

// ---------------------------------------------------------------------------
// Patching: drag from any jack to a jack of the other direction (output → input
// or input → output), or tap one then the other. Long-press (or right-click) a
// jack for its options. Cables hang on damped springs and show signal flow.
// cable = [id, srcModule, srcPort, dstModule, dstPort, gain, colourIndex?]
// ---------------------------------------------------------------------------
let cables = [], armedJack = null, nextCableId = 0, drag = null;
const CABLE_COLOURS = ['#d8332a', '#f2c230', '#2f7ad8', '#3da75a', '#ef7d22', '#e9e7df', '#9b59b6', '#1f1f1f'];
const COLOUR_NAMES = ['red', 'yellow', 'blue', 'green', 'orange', 'white', 'violet', 'black'];
const springs = new Map();   // cable id → {sag, vel}
const systemReduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
let reduceMotion = systemReduceMotion || store.get('batterySaver', false);   // Battery Saving Mode also stops decorative motion
const jackInfo = (j) => ({ m: +j.dataset.m, p: +j.dataset.p, input: j.dataset.in === '1', el: j });
const portName = (m, p, input) => `${rack[m]?.name || '?'} · ${(input ? rack[m]?.ins : rack[m]?.outs)?.[p]?.name || p}`;
function connect(a, b) {
  if (a.input === b.input) { toast('Connect an output to an input'); return; }
  const src = a.input ? b : a, dst = a.input ? a : b;
  cables = cables.filter((c) => !(c[3] === dst.m && c[4] === dst.p));
  const used = new Set(cables.map((c) => c[0])); while (used.has(nextCableId % 64)) nextCableId++;
  const id = nextCableId++ % 64;
  cables.push([id, src.m, src.p, dst.m, dst.p, 1, id % CABLE_COLOURS.length]);
  springs.set(id, { sag: 0, vel: reduceMotion ? 0 : 900 });   // drops in and settles
  publishPatch();
  toast(`${portName(src.m, src.p, false)} → ${portName(dst.m, dst.p, true)}`);
}
function jackClick(e) {
  const j = e.target.closest('.jack'); if (!j || jackClick.suppress) { jackClick.suppress = false; return; }
  const me = jackInfo(j);
  if (!armedJack) { armedJack = me; j.classList.add('armed'); return; }
  const a = armedJack; a.el.classList.remove('armed'); armedJack = null;
  if (a.m === me.m && a.p === me.p && a.input === me.input) return;
  connect(a, me);
}
function rackPoint(e) { const r = $('#rackWrap').getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
function setupPatchGestures(root) {
  let press = null;
  root.addEventListener('pointerdown', (e) => {
    const j = e.target.closest('.jack'); if (!j || e.button > 0) return;
    e.preventDefault();
    press = { j, x: e.clientX, y: e.clientY, id: e.pointerId, moved: false, timer: setTimeout(() => { if (press && !press.moved) { jackClick.suppress = true; jackMenu(jackInfo(j), e.clientX, e.clientY); press = null; } }, 520) };
  });
  window.addEventListener('pointermove', (e) => {
    if (!press || e.pointerId !== press.id) { if (drag) moveDrag(e); return; }
    if (!press.moved && Math.hypot(e.clientX - press.x, e.clientY - press.y) > 8) {
      press.moved = true; clearTimeout(press.timer);
      drag = { from: jackInfo(press.j), pt: rackPoint(e), target: null };
      press.j.classList.add('dragging'); jackClick.suppress = true;
    }
    if (drag) moveDrag(e);
  });
  window.addEventListener('pointerup', (e) => {
    if (press) { clearTimeout(press.timer); press = null; }
    if (!drag) return;
    drag.from.el.classList.remove('dragging'); $$('.jack.target').forEach((x) => x.classList.remove('target'));
    if (drag.target) connect(drag.from, drag.target);
    drag = null; drawCables();
    setTimeout(() => { jackClick.suppress = false; }, 0);
  });
  root.addEventListener('contextmenu', (e) => { const j = e.target.closest('.jack'); if (!j) return; e.preventDefault(); jackMenu(jackInfo(j), e.clientX, e.clientY); });
}
function moveDrag(e) {
  // Auto-scroll while dragging near the top or the keyboard dock.
  const dockTop = document.body.classList.contains('nokeys') ? innerHeight : ($('#dock')?.getBoundingClientRect().top ?? innerHeight);
  if (e.clientY > dockTop - 50) window.scrollBy(0, 18); else if (e.clientY < 140) window.scrollBy(0, -18);
  drag.pt = rackPoint(e);
  const el = document.elementFromPoint(e.clientX, e.clientY)?.closest('.jack');
  $$('.jack.target').forEach((x) => x.classList.remove('target'));
  drag.target = null;
  if (el && (el.dataset.in === '1') !== drag.from.input) { drag.target = jackInfo(el); el.classList.add('target'); }
  drawCables();
}

// Long-press menu ------------------------------------------------------------
function closeMenu() { $('#jackMenu')?.remove(); }
function jackMenu(j, x, y) {
  closeMenu(); armedJack?.el.classList.remove('armed'); armedJack = null;
  const mine = cables.filter((c) => (j.input ? c[3] === j.m && c[4] === j.p : c[1] === j.m && c[2] === j.p));
  const items = [];
  const title = `${portName(j.m, j.p, j.input)} ${j.input ? 'input' : 'output'}`;
  if (mine.length) {
    for (const c of mine) {
      const other = j.input ? portName(c[1], c[2], false) : portName(c[3], c[4], true);
      items.push(`<h4>${j.input ? 'from' : 'to'} ${esc(other)}</h4>`);
      items.push(`<button data-act="rm" data-id="${c[0]}">Remove cable${j.input ? ' (restore default connection)' : ''}</button>`);
      items.push(`<button data-act="gain" data-id="${c[0]}" data-v="1">Level 100 %${c[5] === 1 ? ' ✓' : ''}</button><button data-act="gain" data-id="${c[0]}" data-v="0.5">Level 50 %${c[5] === 0.5 ? ' ✓' : ''}</button><button data-act="gain" data-id="${c[0]}" data-v="-1">Inverted${c[5] === -1 ? ' ✓' : ''}</button>`);
      items.push(`<div class="row" style="padding:4px 8px">${CABLE_COLOURS.map((col, k) => `<button data-act="col" data-id="${c[0]}" data-v="${k}" aria-label="${COLOUR_NAMES[k]} cable" style="width:auto;min-height:36px;padding:4px"><span class="sw8" style="background:${col}"></span></button>`).join('')}</div>`);
    }
    if (mine.length > 1) items.push(`<hr><button data-act="rmall">Remove all ${mine.length} cables</button>`);
    items.push('<hr>');
  }
  // Patch to / from: every jack of the other direction, grouped by module.
  items.push(`<h4>${j.input ? 'patch from an output' : 'patch to an input'}</h4>`);
  rack.forEach((mod, mi) => {
    const ports = j.input ? mod.outs : mod.ins;
    if (!ports.length || !$(`.jack[data-m="${mi}"]`)) return;
    for (const p of ports) items.push(`<button data-act="patch" data-m="${mi}" data-p="${p.index}">${esc(mod.name)} · ${esc(p.name)}</button>`);
  });
  const menu = document.createElement('div');
  menu.className = 'menu'; menu.id = 'jackMenu'; menu.setAttribute('role', 'menu');
  menu.innerHTML = `<h4>${esc(title)}</h4>${items.join('')}`;
  document.body.appendChild(menu);
  const r = menu.getBoundingClientRect();
  menu.style.left = `${clamp(x, 8, innerWidth - r.width - 8)}px`; menu.style.top = `${clamp(y, 8, innerHeight - r.height - 8)}px`;
  menu.querySelector('button')?.focus();
  menu.addEventListener('click', (e) => {
    const b = e.target.closest('button[data-act]'); if (!b) return;
    const id = +b.dataset.id, act = b.dataset.act;
    if (act === 'rm') cables = cables.filter((c) => c[0] !== id);
    if (act === 'rmall') cables = cables.filter((c) => !mine.includes(c));
    if (act === 'gain') cables = cables.map((c) => (c[0] === id ? [c[0], c[1], c[2], c[3], c[4], +b.dataset.v, c[6]] : c));
    if (act === 'col') cables = cables.map((c) => (c[0] === id ? [c[0], c[1], c[2], c[3], c[4], c[5], +b.dataset.v] : c));
    if (act === 'patch') { closeMenu(); connect(j, { m: +b.dataset.m, p: +b.dataset.p, input: !j.input }); return; }
    closeMenu(); publishPatch();
  });
  menu.addEventListener('keydown', (e) => { if (e.key === 'Escape') { closeMenu(); j.el.focus(); } });
}
document.addEventListener('pointerdown', (e) => { if (!e.target.closest('#jackMenu')) closeMenu(); }, true);
document.addEventListener('keydown', (e) => { if ((e.key === 'ContextMenu' || (e.shiftKey && e.key === 'F10')) && e.target.closest('.jack')) { e.preventDefault(); const r = e.target.getBoundingClientRect(); jackMenu(jackInfo(e.target.closest('.jack')), r.left, r.bottom); } });

function publishPatch() { engine.post({ t: 'patch', cables: cables.map((c) => c.slice(0, 6)) }); store.set('cables', cables); drawCables(); saveSession(); }
function jackPos(m, p, input, base) {
  const el = $(`.jack[data-m="${m}"][data-p="${p}"][data-in="${input ? 1 : 0}"] .hole`); if (!el) return null;
  const r = el.getBoundingClientRect(); if (!r.width) return null;
  return [r.left + r.width / 2 - base.left, r.top + r.height / 2 - base.top];
}
let levels = [], swayT = 0;
function cablePath(A, B, sag) { const dx = B[0] - A[0]; return `M${A[0]} ${A[1]} C${A[0] + dx * 0.25} ${A[1] + sag} ${B[0] - dx * 0.25} ${B[1] + sag} ${B[0]} ${B[1]}`; }
function drawCables() {
  const svg = $('#cables'), wrap = $('#rackWrap'); if (!svg || !wrap || !$('#s-rack').classList.contains('on')) return;
  const base = wrap.getBoundingClientRect();
  svg.setAttribute('width', base.width); svg.setAttribute('height', base.height);
  const op = store.get('cableOpacity', 0.85);
  let html = '';
  for (const c of cables) {
    const A = jackPos(c[1], c[2], false, base), B = jackPos(c[3], c[4], true, base); if (!A || !B) continue;
    const dist = Math.hypot(B[0] - A[0], B[1] - A[1]), rest = Math.min(180, 30 + dist * 0.35);
    const sp = springs.get(c[0]) || { sag: rest, vel: 0 };
    springs.set(c[0], sp);
    const sway = reduceMotion ? 0 : Math.sin(swayT * 1.3 + c[0]) * 2.5;
    const d = cablePath(A, B, (sp.sag || rest) + sway);
    const col = CABLE_COLOURS[(c[6] ?? c[0]) % CABLE_COLOURS.length], lv = clamp((levels[c[0]] || 0) * 3, 0, 1);
    html += `<path class="shadow" d="${d}" stroke-width="7" transform="translate(2 4)"/><path d="${d}" stroke="${col}" stroke-width="5" opacity="${op}" data-cable="${c[0]}"><title>${esc(portName(c[1], c[2], false))} → ${esc(portName(c[3], c[4], true))} — long-press a jack for options</title></path>`;
    if (lv > 0.02 && !reduceMotion) html += `<path class="flow" d="${d}" stroke-width="2" opacity="${(0.25 + 0.6 * lv).toFixed(2)}" stroke-dashoffset="${(-swayT * 60 % 16).toFixed(1)}"/>`;
  }
  if (drag) {
    const A = jackPos(drag.from.m, drag.from.p, drag.from.input, base);
    if (A) { const d = cablePath(A, drag.pt, Math.min(120, 20 + Math.hypot(drag.pt[0] - A[0], drag.pt[1] - A[1]) * 0.3)); html += `<path d="${d}" stroke="${drag.target ? 'var(--green)' : 'var(--amber)'}" stroke-width="5" opacity=".9" style="pointer-events:none"/>`; }
  }
  svg.innerHTML = html;
}
// Spring step: each cable's sag settles toward its rest droop; sway and flow run while the rack is visible.
function stepCables(dt) {
  if (!cables.length && !drag) return;
  swayT += dt;
  const wrap = $('#rackWrap'); if (!wrap) return;
  const base = wrap.getBoundingClientRect();
  for (const c of cables) {
    const A = jackPos(c[1], c[2], false, base), B = jackPos(c[3], c[4], true, base); if (!A || !B) continue;
    const rest = Math.min(180, 30 + Math.hypot(B[0] - A[0], B[1] - A[1]) * 0.35);
    const sp = springs.get(c[0]); if (!sp) continue;
    if (reduceMotion) { sp.sag = rest; continue; }
    sp.vel += ((rest - sp.sag) * 90 - sp.vel * 7) * dt; sp.sag += sp.vel * dt;
  }
  drawCables();
}
document.addEventListener('click', (e) => { const p = e.target.closest('path[data-cable]'); if (!p) return; const c = cables.find((x) => x[0] === +p.dataset.cable); if (c) { const r = e.clientX; jackMenu({ m: c[3], p: c[4], input: true, el: $(`.jack[data-m="${c[3]}"][data-p="${c[4]}"][data-in="1"]`) }, e.clientX, e.clientY); } });
window.addEventListener('resize', drawCables);

// ---------------------------------------------------------------------------
// Keyboard dock
// ---------------------------------------------------------------------------
let base = store.get('octave', 36), hold = false;
const held = new Set(), pointerNotes = new Map();
function buildKeys() {
  const k = $('#keys'), n = innerWidth < 700 ? 25 : 37, whites = [];
  for (let i = 0; i < n; i++) if (![1, 3, 6, 8, 10].includes((base + i) % 12)) whites.push(base + i);
  const ww = 100 / whites.length;
  let html = '';
  whites.forEach((note, i) => { html += `<div class="key" data-n="${note}" style="left:${i * ww}%;width:${ww}%">${note % 12 === 0 ? `<small>C${note / 12 - 1}</small>` : ''}</div>`; });
  for (let i = 0; i < n; i++) {
    const note = base + i; if (![1, 3, 6, 8, 10].includes(note % 12)) continue;
    const wi = whites.indexOf(note - 1); html += `<div class="key b" data-n="${note}" style="left:${(wi + 0.68) * ww}%;width:${ww * 0.64}%"></div>`;
  }
  k.innerHTML = html;
  $('#octave').textContent = `C${base / 12 - 1}–C${(base + n - 1) / 12 - 1}`;
  for (const n2 of held) keyLight(n2, true);
}
function noteOn(n, v = +$('#vel').value) { if (n < 0 || n > 127) return; held.add(n); engine.post({ t: 'midi', d: [0x90, n, v] }); keyLight(n, true); }
function noteOff(n) { if (hold) return; held.delete(n); engine.post({ t: 'midi', d: [0x80, n, 0] }); keyLight(n, false); }
function keyLight(n, on) { const el = $(`.key[data-n="${n}"]`); if (el) el.classList.toggle('on', on); }
function setupDock() {
  buildKeys();
  const k = $('#keys');
  const noteAt = (e) => { const el = document.elementFromPoint(e.clientX, e.clientY)?.closest('.key'); return el ? +el.dataset.n : null; };
  k.addEventListener('pointerdown', (e) => {
    k.setPointerCapture(e.pointerId); const n = noteAt(e); if (n == null) return;
    const r = e.target.getBoundingClientRect(), vel = clamp(Math.round(40 + 87 * (e.clientY - r.top) / r.height), 1, 127);
    pointerNotes.set(e.pointerId, n); noteOn(n, e.pointerType === 'mouse' ? +$('#vel').value : vel);
  });
  k.addEventListener('pointermove', (e) => { if (!pointerNotes.has(e.pointerId)) return; const n = noteAt(e), o = pointerNotes.get(e.pointerId); if (n != null && n !== o) { noteOff(o); noteOn(n); pointerNotes.set(e.pointerId, n); } });
  const up = (e) => { if (pointerNotes.has(e.pointerId)) { noteOff(pointerNotes.get(e.pointerId)); pointerNotes.delete(e.pointerId); } };
  k.addEventListener('pointerup', up); k.addEventListener('pointercancel', up);
  $('#octDown').addEventListener('click', () => { base = Math.max(0, base - 12); store.set('octave', base); buildKeys(); });
  $('#octUp').addEventListener('click', () => { base = Math.min(84, base + 12); store.set('octave', base); buildKeys(); });
  $('#hold').addEventListener('click', (e) => { hold = !hold; e.currentTarget.setAttribute('aria-pressed', hold); if (!hold) for (const n of [...held]) { held.delete(n); engine.post({ t: 'midi', d: [0x80, n, 0] }); keyLight(n, false); } });
  // Computer keyboard: A W S E D F T G Y H U J K O L P ;
  const map = 'awsedftgyhujkolp;';
  window.addEventListener('keydown', (e) => {
    if (e.repeat || e.metaKey || e.ctrlKey || /input|select|textarea/i.test(e.target.tagName)) return;
    const i = map.indexOf(e.key.toLowerCase()); if (i >= 0) { noteOn(base + 12 + i); e.preventDefault(); }
    if (e.key === 'z') { base = Math.max(0, base - 12); buildKeys(); } if (e.key === 'x') { base = Math.min(84, base + 12); buildKeys(); }
  });
  window.addEventListener('keyup', (e) => { const i = map.indexOf(e.key.toLowerCase()); if (i >= 0) noteOff(base + 12 + i); });
  // Wheels
  const wheel = (el, spring, send) => {
    let id = null, y0 = 0, n0 = 0;
    const set = (n) => { n = clamp(n, 0, 1); el.style.setProperty('--n', n); el.setAttribute('aria-valuenow', n.toFixed(2)); send(n); el.dataset.n = n; };
    el.addEventListener('pointerdown', (e) => { id = e.pointerId; el.setPointerCapture(id); y0 = e.clientY; n0 = +(el.dataset.n ?? (spring ? 0.5 : 0)); });
    el.addEventListener('pointermove', (e) => { if (e.pointerId === id) set(n0 + (y0 - e.clientY) / el.clientHeight); });
    const end = (e) => { if (e.pointerId === id) { id = null; if (spring) set(0.5); } };
    el.addEventListener('pointerup', end); el.addEventListener('pointercancel', end);
    el.addEventListener('keydown', (e) => { if (e.key === 'ArrowUp') set(+(el.dataset.n ?? 0) + 0.05); if (e.key === 'ArrowDown') set(+(el.dataset.n ?? 0) - 0.05); });
  };
  wheel($('#bend'), true, (n) => { const v = Math.round(n * 16383); engine.post({ t: 'midi', d: [0xE0, v & 127, v >> 7] }); });
  wheel($('#modw'), false, (n) => engine.post({ t: 'midi', d: [0xB0, 1, Math.round(n * 127)] }));
}

// VU meters (cream face, needle).
function vuFace(el, label) {
  const ticks = [-20, -10, -7, -5, -3, -1, 0, 1, 2, 3].map((db) => {
    const a = (vuAngle(db) - 90) * Math.PI / 180, x1 = 60 + Math.cos(a) * 52, y1 = 70 + Math.sin(a) * 52, x2 = 60 + Math.cos(a) * 60, y2 = 70 + Math.sin(a) * 60;
    return `<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}" stroke="${db > 0 ? '#c2281f' : '#222'}" stroke-width="1"/>`;
  }).join('');
  el.innerHTML = `<svg viewBox="0 0 120 72" aria-hidden="true"><path d="M${60 + Math.cos((vuAngle(0) - 90) * Math.PI / 180) * 56} ${70 + Math.sin((vuAngle(0) - 90) * Math.PI / 180) * 56} A56 56 0 0 1 ${60 + Math.cos((vuAngle(3) - 90) * Math.PI / 180) * 56} ${70 + Math.sin((vuAngle(3) - 90) * Math.PI / 180) * 56}" stroke="#c2281f" stroke-width="4" fill="none"/>${ticks}
    <text x="60" y="44" text-anchor="middle" font-size="10" font-family="Archivo Narrow" fill="#222">VU</text><text x="60" y="56" text-anchor="middle" font-size="7" font-family="Archivo Narrow" fill="#555">${label}</text>
    <line class="needle" x1="60" y1="70" x2="60" y2="12" stroke="#111" stroke-width="1.2"/><circle cx="60" cy="70" r="5" fill="#111"/></svg>`;
}
function vuAngle(db) { return clamp(-48 + (db + 20) / 23 * 96, -50, 50); }
const vu = [{ v: -20 }, { v: -20 }];
function vuUpdate(peaks) {
  [$('#vuL'), $('#vuR')].forEach((el, i) => {
    const db = peaks ? 20 * Math.log10(Math.max(peaks[i], 1e-5)) + 6 : -40;   // ≈ VU with +6 dB headroom alignment
    vu[i].v += (Math.max(-21, db) - vu[i].v) * 0.3;
    const n = el.querySelector('.needle'); if (n) n.setAttribute('transform', `rotate(${vuAngle(vu[i].v)} 60 70)`);
  });
}

// ---------------------------------------------------------------------------
// Engine status → UI
// ---------------------------------------------------------------------------
let lastStatus = null;
function onEngine(m) {
  if (m.t === 'status') { lastStatus = m; for (const [a, v] of m.changed) setParam(a, v, { fromEngine: true }); for (const e of m.ui) uiEvent(e); return; }
  if (m.t === 'values') { for (const [a, v] of m.pairs) setParam(a, v, { fromEngine: true, quiet: true }); for (let s = 0; s < 8; s++) relabelSlot(s); return; }
  if (m.t === 'midiout') { if (midiOut) for (const [ms, ...bytes] of m.ev) { try { midiOut.send(bytes, performance.now() + ms + 10); } catch {} } return; }
  if (m.t === 'patched') { if (m.rejected) toast(`${m.rejected} cable(s) could not be used (that input takes no cable)`); return; }
  if (m.t === 'audio') { rec.chunks.push(...m.chunks); return; }
  if (m.t === 'log') console.log('[kernel]', m.s);
}
function uiEvent(e) {
  if (e.kind === 0) learned(e);
  else if (e.kind === 1 && factory && e.msb <= 1) { const list = factory.presets.filter((p) => p.bank === e.msb); if (list[e.number]) loadFactory(list[e.number]); }
  else if (e.kind === 1 && e.msb === 2) { const all = store.get('presets', {}), names = Object.keys(all).sort((a, b) => a.localeCompare(b)); if (names[e.number]) { applyState(all[names[e.number]]); toast(`Program ${e.number + 1}: ${names[e.number]}`); } }
}
const SYNC = ['internal tempo', 'synced to play', 'midi clock'];
function frame(t) {
  const s = lastStatus;
  vuUpdate(s && s.peak);
  if (s) {
    $('#cpu').textContent = s.load == null ? `${s.voices} voices` : `${(s.load * 100).toFixed(0)} % · ${s.voices} v`;
    $('#midiIn').textContent = `midi ${s.midi.inN}`;
    if ($('#monLast')) {
      const [st, a, b] = s.midi.last;
      $('#monLast').textContent = st ? `${['note off', 'note on', 'poly at', 'cc', 'program', 'pressure', 'bend'][(st >> 4) - 8] || 'msg'} ch ${(st & 15) + 1} · ${a} ${b}` : '—';
      $('#monCount').textContent = `in ${s.midi.inN} · out ${s.midi.outN}`;
      $('#monSync').textContent = s.midi.sync === 2 ? `midi clock ${s.midi.tempo.toFixed(1)}` : SYNC[s.midi.sync];
    }
    // Step LEDs, LFO and gate lamps
    $$('[data-step]').forEach((el) => el.classList.toggle('on', +el.dataset.step === s.seq.step));
    const gate = s.held.some((w) => w) || s.voices > 0;
    $$('[data-led="gate"]').forEach((el) => el.classList.toggle('on', gate));
    for (const n of [1, 2, 3]) { const r = values.get(P[`lfo${n}.rate`].a) || 1; $$(`[data-led="lfo${n}"]`).forEach((el) => el.classList.toggle('on', Math.sin(t / 1000 * 2 * Math.PI * Math.min(r, 25)) > 0)); }
    $$('[data-led="arp"]').forEach((el) => el.classList.toggle('on', !!(s.seq.flags & 1) && Math.floor(s.seq.beat * 4) % 2 === 0));
    $$('[data-led="gen"]').forEach((el) => el.classList.toggle('on', !!(s.seq.flags & 4)));
    if ($('#genBar')) $('#genBar').textContent = s.seq.flags & 4 ? `generative: phrase ${s.seq.phrase + 1}, bar ${s.seq.bar + 1}` : '';
    s.fx.forEach((v, i) => { const el = $(`[data-fxled="${i}"]`); if (el) el.classList.toggle('on', v > 0.001); });
    if ($('#fxLatency')) $('#fxLatency').textContent = s.latency ? `Effects latency ${s.latency} samples, compensated on every path.` : 'No added latency.';
    s.mods.forEach((v, i) => { const el = $(`[data-mod="${i}"]`); if (el) el.style.width = `${clamp(Math.abs(v), 0, 1) * 100}%`; });
    levels = s.cables;
    const notes = []; s.held.forEach((w, i) => { for (let b = 0; b < 32; b++) if (w & (1 << b)) notes.push(i * 32 + b); });
    $('#chord').textContent = notes.length >= 2 && ui ? (chordName(notes) || '—') : notes.length === 1 ? noteName(notes[0]) : '—';
  }
  const dt = Math.min(0.05, (t - (frame.prev || t)) / 1000); frame.prev = t;
  const cableMs = store.get('batterySaver', false) ? 66 : 33;
  if ($('#s-rack').classList.contains('on') && t - (frame.lc || 0) > cableMs) { stepCables(Math.max(dt, cableMs / 1000)); frame.lc = t; }
  requestAnimationFrame(frame);
}
function noteName(n) { return ['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'][n % 12] + (Math.floor(n / 12) - 1); }
function chordName(notes) { const p = ui.malloc(notes.length); new Uint8Array(ui.memory.buffer, p, notes.length).set(notes); const s = cstr(ui.vkw_chord(p, notes.length)); ui.free(p); return s; }

// ---------------------------------------------------------------------------
// Routing between screens (#name)
// ---------------------------------------------------------------------------
function route() {
  const name = (location.hash || '#rack').slice(1), id = `s-${name}`;
  const target = document.getElementById(id) ? id : 's-rack';
  $$('.screen').forEach((s) => s.classList.toggle('on', s.id === target));
  $$('nav.tabs a').forEach((a) => { if (a.getAttribute('href') === '#' + target.slice(2)) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current'); });
  requestAnimationFrame(drawCables);
}
window.addEventListener('hashchange', route);

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
async function boot() {
  // Single-file build (tools/bundle_web.py) carries everything in VK_BUNDLE.
  const B = globalThis.VK_BUNDLE;
  const [pj, panelJ, bytes, presetJ, manualJ] = B
    ? [B.params, B.panels, Uint8Array.from(atob(B.wasm), (c) => c.charCodeAt(0)).buffer, B.presets, B.manual]
    : await Promise.all([fetch('params.json').then((r) => r.json()), fetch('panels.json').then((r) => r.json()), fetch('kernel.wasm').then((r) => r.arrayBuffer()),
        fetch('presets.json').then((r) => r.json()).catch(() => null), fetch('manual.json').then((r) => r.json()).catch(() => [])]);
  factory = presetJ; manual = manualJ || [];
  wasmBytes = bytes; panels = panelJ; virtualDests = pj.virtualDests || {};
  for (const [k, n] of pj.groups) groups[k] = n;
  for (const p of pj.params) { p.mod = !!p.m || false; P[p.id] = p; byAddr.set(p.a, p); values.set(p.a, p.d); }
  // UI-side instance: catalogue, rack, formatting, chords (never renders audio).
  const { instance } = await WebAssembly.instantiate(bytes, { env: wasmEnv(() => ui.memory) });
  ui = instance.exports; L = readLayout(ui);
  const dv = () => new DataView(ui.memory.buffer);
  for (let m = 0, n = ui.vk_rack_module_count(); m < n; m++) {
    const ip = ui.vkw_module_info(m), d = dv();
    const info = { key: cstr(d.getUint32(ip + L.mod.key, true)), name: cstr(d.getUint32(ip + L.mod.name, true)), ins: [], outs: [] };
    const ni = d.getUint8(ip + L.mod.ins), no = d.getUint8(ip + L.mod.outs);
    for (const [cnt, input, list] of [[ni, 1, info.ins], [no, 0, info.outs]])
      for (let p = 0; p < cnt; p++) { const pp = ui.vkw_port_info(m, p, input), d2 = dv(); list.push({ index: p, key: cstr(d2.getUint32(pp + L.port.key, true)), name: cstr(d2.getUint32(pp + L.port.name, true)), kind: d2.getUint8(pp + L.port.kind) }); }
    rack.push(info);
  }
  for (let t = 0, n = ui.vk_fx_type_count(); t < n; t++) fxTypes.push({ name: cstr(ui.vkw_fx_type_name(t)), family: cstr(ui.vkw_fx_type_family(t)) });

  // Restore the browser session.
  const sess = store.get('session', null);
  if (sess?.parameters) for (const p of Object.values(P)) { const v = sess.parameters[p.id]; if (typeof v === 'number' && Number.isFinite(v)) values.set(p.a, quant(p, v)); }
  cables = store.get('cables', sess?.cables || []);

  buildRack(); buildSequencer(); buildArp(); buildEffects(); buildMatrix(); buildMidi(); buildPresets(); buildAbout(); buildHelp(); buildPhase9(); setupDock();
  vuFace($('#vuL'), 'left'); vuFace($('#vuR'), 'right');
  for (let s = 0; s < 8; s++) relabelSlot(s);
  publishMappings();
  route();
  const wear = store.get('wear', 0.35); $('#wear').value = wear; document.documentElement.style.setProperty('--wear', wear);
  $('#wear').addEventListener('input', (e) => { document.documentElement.style.setProperty('--wear', e.target.value); store.set('wear', +e.target.value); });
  $('#bootMsg').textContent = 'Ready. Press power to start the sound (browsers need a click before audio).';
  $('#start').disabled = false;
  requestAnimationFrame(frame);
}

async function powerOn() {
  if (audio) { if (audio.state === 'suspended') await audio.resume(); return; }
  audio = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  let ok = false;
  try {
    const src = globalThis.VK_BUNDLE?.worklet;
    await audio.audioWorklet.addModule(src ? URL.createObjectURL(new Blob([src], { type: 'text/javascript' })) : 'worklet.js');
    const node = new AudioWorkletNode(audio, 'vektur', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { bytes: wasmBytes.slice(0) } });
    node.onprocessorerror = (e) => { console.error('processor error', e); toast('The audio engine stopped (processor error)'); };
    node.connect(audio.destination);
    node.port.onmessage = (e) => onEngine(e.data);
    engine = { post: (m) => node.port.postMessage(m) };
    ok = true;
  } catch (err) {
    console.warn('AudioWorklet unavailable, using the main-thread fallback', err);
  }
  if (!ok) {   // ScriptProcessor fallback: the same engine on the main thread.
    const eng = new Engine(wasmBytes.slice(0), audio.sampleRate, onEngine);
    const sp = audio.createScriptProcessor(512, 0, 2);
    const l = new Float32Array(128), r = new Float32Array(128);
    sp.onaudioprocess = (ev) => { const L0 = ev.outputBuffer.getChannelData(0), R0 = ev.outputBuffer.getChannelData(1); for (let o = 0; o < L0.length; o += 128) { eng.render(l, r); L0.set(l, o); R0.set(r, o); } };
    sp.connect(audio.destination);
    engine = { post: (m) => eng.message(m) };
    toast('Running without AudioWorklet (higher latency)');
  }
  // Send the whole state to the fresh kernel.
  engine.post({ t: 'addrs', list: Object.values(P).map((p) => p.a) });
  for (const p of Object.values(P)) { const v = values.get(p.a); if (v !== p.d) engine.post({ t: 'set', a: p.a, v }); }
  engine.post({ t: 'patch', cables });
  publishMappings();
  sendTransport();
  $('#power').setAttribute('aria-pressed', 'true');
  $('#splash').remove();
}
function sendTransport() {
  const [num, den] = $('#meter').value.split('/').map(Number);
  engine.post({ t: 'transport', playing: $('#play').getAttribute('aria-pressed') === 'true', tempo: clamp(+$('#tempo').value || 120, 20, 300), num, den });
}
$('#start').addEventListener('click', powerOn);
$('#power').addEventListener('click', async () => { if (!audio) return powerOn(); if (audio.state === 'running') { await audio.suspend(); $('#power').setAttribute('aria-pressed', 'false'); } else { await audio.resume(); $('#power').setAttribute('aria-pressed', 'true'); } });
$('#play').addEventListener('click', (e) => { const on = e.currentTarget.getAttribute('aria-pressed') !== 'true'; e.currentTarget.setAttribute('aria-pressed', on); sendTransport(); });
$('#tempo').addEventListener('change', () => { setParam(P['global.tempo'].a, +$('#tempo').value); sendTransport(); });
$('#meter').addEventListener('change', sendTransport);
// Recording (Phase 9): 24-bit WAV of the output.
const rec = { on: false, chunks: [], t0: 0 };
// WAV writer: 16-bit, 24-bit PCM or 32-bit float (format 3), stereo.
function wavFile(chunks, sr, bits = 24) {
  const frames = chunks.reduce((n, c, i) => n + (i % 2 === 0 ? c.length : 0), 0), bps = bits / 8, block = 2 * bps;
  const buf = new ArrayBuffer(44 + frames * block), d = new DataView(buf);
  const str = (o, t) => { for (let i = 0; i < t.length; i++) d.setUint8(o + i, t.charCodeAt(i)); };
  str(0, 'RIFF'); d.setUint32(4, 36 + frames * block, true); str(8, 'WAVEfmt '); d.setUint32(16, 16, true); d.setUint16(20, bits === 32 ? 3 : 1, true); d.setUint16(22, 2, true);
  d.setUint32(24, sr, true); d.setUint32(28, sr * block, true); d.setUint16(32, block, true); d.setUint16(34, bits, true); str(36, 'data'); d.setUint32(40, frames * block, true);
  let o = 44;
  for (let i = 0; i < chunks.length; i += 2) {
    const l = chunks[i], r = chunks[i + 1];
    for (let k = 0; k < l.length; k++) for (const v of [l[k], r[k]]) {
      if (bits === 32) { d.setFloat32(o, v, true); o += 4; continue; }
      if (bits === 16) { d.setInt16(o, Math.round(clamp(v, -1, 1) * 32767), true); o += 2; continue; }
      const q = Math.round(clamp(v, -1, 1) * 8388607); d.setUint8(o, q & 255); d.setUint8(o + 1, (q >> 8) & 255); d.setUint8(o + 2, (q >> 16) & 255); o += 3;
    }
  }
  return new Blob([buf], { type: 'audio/wav' });
}
const wav24 = (chunks, sr) => wavFile(chunks, sr, 24);
$('#rec').addEventListener('click', (e) => {
  if (!audio) { toast('Power on first'); return; }
  rec.on = !rec.on; e.currentTarget.setAttribute('aria-pressed', rec.on);
  if (rec.on) { rec.chunks = []; rec.t0 = performance.now(); engine.post({ t: 'record', on: true }); toast('Recording…'); return; }
  engine.post({ t: 'record', on: false });
  setTimeout(() => {
    const bits = store.get('recBits', 24), blob = wavFile(rec.chunks, audio.sampleRate, bits), url = URL.createObjectURL(blob);
    const secs = ((performance.now() - rec.t0) / 1000).toFixed(1);
    const a = document.createElement('a'); a.href = url; a.download = `Vektur ${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.wav`;
    document.body.appendChild(a); a.click(); a.remove();
    toast(`Recorded ${secs} s (${bits === 32 ? '32-bit float' : bits + '-bit'} WAV, ${audio.sampleRate} Hz). If no download started, the page is embedded — open the single-file version.`);
  }, 250);
});
const setKeys = (on) => { document.body.classList.toggle('nokeys', !on); $('#keysToggle').setAttribute('aria-pressed', on); $('#keysToggle .lamp').classList.toggle('on', on); $('#showKeys').hidden = on; store.set('keys', on); requestAnimationFrame(drawCables); };
$('#keysToggle').addEventListener('click', () => setKeys(document.body.classList.contains('nokeys')));
$('#hideKeys').addEventListener('click', () => setKeys(false));
$('#showKeys').addEventListener('click', () => setKeys(true));
setKeys(store.get('keys', true));
$('#panic').addEventListener('click', () => { engine.post({ t: 'cmd', type: 1 }); held.clear(); $$('.key.on').forEach((k) => k.classList.remove('on')); });
window.vektur = { get audio() { return audio; }, get status() { return lastStatus; }, setParam, snapshot: () => snapshot(), get values() { return values; }, P };
boot().catch((e) => { $('#bootMsg').textContent = 'The engine could not load: ' + e.message; console.error(e); });


// ---------------------------------------------------------------------------
// Phase 9/10 (web): manual with contextual help, MIDI reference export,
// preset / project / pattern files in the app's exchange format, recording
// depth, oscillator quality, filter oversampling and Battery Saving Mode.
// ---------------------------------------------------------------------------
const HELP_FOR = { rack: 'signal-flow', sequencer: 'sequencer', arp: 'arpeggiator', effects: 'effects', matrix: 'mod-matrix', midi: 'midi-input', presets: 'presets', about: 'performance-settings', help: 'welcome' };
function mdToHtml(md) {
  const inl = (t) => esc(t).replace(/`([^`]+)`/g, '<code>$1</code>').replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>').replace(/\*([^*]+)\*/g, '<i>$1</i>');
  const out = []; let para = [], list = null, table = null;
  const flush = () => { if (para.length) out.push(`<p>${inl(para.join(' '))}</p>`); para = []; if (list) out.push(`<${list.t}>${list.items.map((x) => `<li>${inl(x)}</li>`).join('')}</${list.t}>`); list = null;
    if (table) out.push(`<div class="scroll"><table class="matrix">${table.map((r, i) => `<tr>${r.map((c) => i ? `<td>${inl(c)}</td>` : `<th>${inl(c)}</th>`).join('')}</tr>`).join('')}</table></div>`); table = null; };
  for (const raw of md.split('\n')) {
    const l = raw.trim();
    if (!l) { flush(); continue; }
    if (l.startsWith('|')) { const cells = l.replace(/^\||\|$/g, '').split('|').map((c) => c.trim()); if (!cells.every((c) => /^[-:]+$/.test(c))) (table ||= []).push(cells); continue; }
    if (l.startsWith('#')) { flush(); out.push(`<h3>${inl(l.replace(/^#+\s*/, ''))}</h3>`); continue; }
    let m;
    if ((m = l.match(/^- (.*)/)) || (m = l.match(/^\d+\. (.*)/))) { const t = l.startsWith('-') ? 'ul' : 'ol'; if (para.length) { out.push(`<p>${inl(para.join(' '))}</p>`); para = []; } if (!list || list.t !== t) { if (list) flush(); list = { t, items: [] }; } list.items.push(m[1]); continue; }
    para.push(l);
  }
  flush();
  return out.join('');
}
function buildHelp() {
  if (!$('#s-help')) {
    const sec = document.createElement('section'); sec.className = 'screen'; sec.id = 's-help'; sec.setAttribute('aria-label', 'Manual'); $('#main').appendChild(sec);
    const a = document.createElement('a'); a.href = '#help'; a.textContent = 'manual'; $('nav.tabs').insertBefore(a, $('nav.tabs a[href="#about"]'));
    const b = document.createElement('button'); b.className = 'tbtn'; b.id = 'helpBtn'; b.title = 'Help for this screen (?)'; b.setAttribute('aria-label', 'Help for this screen'); b.textContent = '?';
    $('#keysToggle').after(b);
    b.addEventListener('click', () => { const here = (location.hash || '#rack').slice(1); openChapter(HELP_FOR[here] || 'welcome'); });
    document.addEventListener('keydown', (e) => { if (e.key === '?' && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement?.tagName || '')) { e.preventDefault(); b.click(); } });
  }
  $('#s-help').innerHTML = `<div class="sheet" style="display:grid;grid-template-columns:minmax(180px,260px) 1fr;gap:18px" id="manualSheet">
    <nav aria-label="Chapters"><input type="search" id="manualSearch" placeholder="Search the manual" aria-label="Search the manual" style="width:100%;margin-bottom:8px">
    <ol id="manualToc" style="margin:0;padding-left:22px;line-height:1.7">${manual.map((c) => `<li><a href="#help" data-ch="${c.slug}">${esc(c.title)}</a></li>`).join('')}</ol></nav>
    <article id="manualBody" tabindex="-1" style="max-width:760px"></article></div>`;
  $('#manualToc').addEventListener('click', (e) => { const a = e.target.closest('[data-ch]'); if (a) { e.preventDefault(); openChapter(a.dataset.ch); } });
  $('#manualSearch').addEventListener('input', (e) => { const w = e.target.value.toLowerCase().split(/\s+/).filter(Boolean);
    $$('#manualToc li').forEach((li, i) => { li.hidden = w.length > 0 && !w.every((x) => manual[i].md.toLowerCase().includes(x)); }); });
  if (manual.length) showChapter(manual[0].slug);
}
function showChapter(slug) {
  const i = manual.findIndex((c) => c.slug === slug); if (i < 0) return;
  const c = manual[i], prev = manual[i - 1], next = manual[i + 1];
  $('#manualBody').innerHTML = mdToHtml(c.md) + `<div class="row" style="justify-content:space-between;margin-top:18px">${prev ? `<button class="tbtn" data-go="${prev.slug}">‹ ${esc(prev.title)}</button>` : '<span></span>'}${next ? `<button class="tbtn" data-go="${next.slug}">${esc(next.title)} ›</button>` : ''}</div>`;
  $$('#manualBody [data-go]').forEach((b) => b.addEventListener('click', () => showChapter(b.dataset.go)));
  $$('#manualToc a').forEach((a) => a.toggleAttribute('aria-current', a.dataset.ch === slug));
}
function openChapter(slug) { location.hash = '#help'; showChapter(slug); setTimeout(() => $('#manualBody')?.focus(), 30); }

function saveFile(name, text, type = 'application/json') {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement('a'); a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}
const SETUP = /^global\.(sync|receive|velocityCurve|programChange|midi|mpe|zone|split|tempo)/;
// The app's InstrumentState / ExchangeFile shape (VekturCore/Exchange.swift).
function appState(kind) {
  const parameters = {};
  for (const p of Object.values(P)) {
    const v = values.get(p.a) ?? p.d;
    if (kind === 'pattern' && !/^(seq|arp)\./.test(p.id)) continue;
    if (kind === 'preset' && SETUP.test(p.id)) continue;
    if (v !== p.d || kind === 'project') parameters[p.id] = v;
  }
  const patch = { cables: kind === 'pattern' ? [] : cables.map((c) => ({ id: c[0], from: { module: rack[c[1]].key, port: rack[c[1]].outs[c[2]].key }, to: { module: rack[c[3]].key, port: rack[c[3]].ins[c[4]].key }, gain: c[5], locked: false })) };
  const midi = { mappings: kind === 'pattern' ? [] : mappings.map((m) => ({ ...m, id: m.id || crypto.randomUUID?.() || String(Math.random()) })) };
  return { schemaVersion: 1, writtenBy: 'web 1.0.0', parameters, patch, clips: [], midi };
}
function exportExchange(kind) {
  const name = $('#presetName')?.value.trim() || 'Vektur';
  const file = { format: `vektur.${kind}`, schemaVersion: 1, writtenBy: 'web 1.0.0', name, state: appState(kind) };
  saveFile(`${name} ${kind}.vektur${kind}`, JSON.stringify(file, null, 1));
  toast(`Exported ${kind} — opens in the app too`);
}
function importExchange(text) {
  let f; try { f = JSON.parse(text); } catch { toast('That is not a Vektur file'); return; }
  const kind = (f.format || '').replace(/^vektur\./, '');
  if (!['preset', 'project', 'pattern'].includes(kind) || !f.state) { toast('That is not a Vektur preset, project or pattern file'); return; }
  if ((f.schemaVersion || 1) > 1) { toast('This file was written by a newer version'); return; }
  const st = f.state;
  // A preset replaces the sound (setup kept); a project replaces everything; a pattern only seq/arp.
  if (kind !== 'pattern') for (const p of Object.values(P)) if (kind === 'project' || !SETUP.test(p.id)) setParam(p.a, p.d, { quiet: true });
  for (const [id, v] of Object.entries(st.parameters || {})) {
    if (!P[id] || (kind === 'pattern' && !/^(seq|arp)\./.test(id)) || (kind === 'preset' && SETUP.test(id))) continue;
    if (typeof v === 'number' && Number.isFinite(v)) setParam(P[id].a, v, { quiet: true });
  }
  if (kind !== 'pattern') {
    const key = (r) => { const mi = rack.findIndex((x) => x.key === r?.module); return mi; };
    cables = [];
    for (const c of st.patch?.cables || []) {
      const a = key(c.from), b = key(c.to); if (a < 0 || b < 0) continue;
      const sp = rack[a].outs.find((x) => x.key === c.from.port), dp = rack[b].ins.find((x) => x.key === c.to.port); if (!sp || !dp) continue;
      const id = cables.length; cables.push([id, a, sp.index, b, dp.index, typeof c.gain === 'number' ? c.gain : 1, id]); springs.set(id, { sag: 0, vel: 600 });
    }
    publishPatch();
    for (let s = 0; s < 8; s++) relabelSlot(s);
  }
  saveSession();
  toast(`Imported ${kind} “${f.name || ''}”`);
}
function referenceRows() {
  const byParam = {}; for (const m of mappings) (byParam[m.parameter] ||= []).push(`${m.source === 'cc' || !m.source ? 'CC' : m.source} ${m.number}${m.channel == null ? '' : ` (ch ${m.channel + 1})`}`);
  return Object.values(P).filter((p) => p.auto !== false).map((p) => ({ id: p.id, name: p.n, group: p.g, addr: p.a, range: p.s ? `${p.s[0]} … ${p.s[p.s.length - 1]}` : `${p.lo} … ${p.hi}${p.u ? ' ' + p.u : ''}`, midi: byParam[p.id] || [] }));
}
function buildPhase9() {
  // Presets: file import / export (app format).
  const ps = $('#s-presets');
  ps.insertAdjacentHTML('beforeend', `<div class="sheet"><h3>files</h3><p>Preset, project and pattern files use the app's format: export here, open in Vektur Modular on iPhone, iPad or Mac (and back).</p>
    <div class="row"><button class="tbtn" data-exp="preset">export preset</button><button class="tbtn" data-exp="project">export project</button><button class="tbtn" data-exp="pattern">export pattern</button>
    <label class="tbtn" style="cursor:pointer">import file…<input type="file" id="exImport" accept=".vekturpreset,.vekturproject,.vekturpattern,.json" hidden></label></div></div>`);
  ps.addEventListener('click', (e) => { const b = e.target.closest('[data-exp]'); if (b) exportExchange(b.dataset.exp); });
  $('#exImport').addEventListener('change', async (e) => { const f = e.target.files[0]; if (f) importExchange(await f.text()); e.target.value = ''; });
  // MIDI reference.
  $('#s-midi').insertAdjacentHTML('beforeend', `<div class="sheet"><h3>midi & automation reference</h3><p>Every automatable parameter, its stable address and the controllers mapped to it.</p>
    <div class="row"><input type="search" id="refSearch" placeholder="Parameter, identifier or CC" aria-label="Search the reference"><label class="tfield"><input type="checkbox" id="refMapped"> mapped only</label>
    <button class="tbtn" id="refCsv">export csv</button><button class="tbtn" id="refMd">export markdown</button></div><div class="scroll" style="max-height:420px"><table class="matrix" id="refTable"></table></div></div>`);
  const renderRef = () => {
    const q = $('#refSearch').value.toLowerCase(), only = $('#refMapped').checked;
    const rows = referenceRows().filter((r) => (!only || r.midi.length) && (!q || r.name.toLowerCase().includes(q) || r.id.toLowerCase().includes(q) || r.midi.join(' ').toLowerCase().includes(q)));
    $('#refTable').innerHTML = '<tr><th>parameter</th><th>identifier</th><th>address</th><th>range</th><th>midi</th></tr>' + rows.slice(0, 400).map((r) => `<tr><td>${esc(r.name)}</td><td><code>${esc(r.id)}</code></td><td>${r.addr}</td><td>${esc(r.range)}</td><td>${esc(r.midi.join(', ') || '—')}</td></tr>`).join('');
  };
  $('#refSearch').addEventListener('input', renderRef); $('#refMapped').addEventListener('change', renderRef); renderRef();
  $('#refCsv').addEventListener('click', () => { const q = (s) => `"${String(s).replace(/"/g, '""')}"`;
    saveFile('Vektur MIDI reference.csv', 'identifier,name,group,address,range,midi\n' + referenceRows().map((r) => [r.id, r.name, r.group, r.addr, r.range, r.midi.join('; ')].map(q).join(',')).join('\n') + '\n', 'text/csv'); });
  $('#refMd').addEventListener('click', () => { const rows = referenceRows(); let md = `# Vektur Modular — MIDI & automation reference\n\n${rows.length} automatable parameters.\n`;
    const g = {}; for (const r of rows) (g[r.group] ||= []).push(r);
    for (const k of Object.keys(g).sort()) { md += `\n## ${k}\n\n| Parameter | Identifier | Address | Range | MIDI |\n|---|---|---:|---|---|\n`; for (const r of g[k]) md += `| ${r.name} | \`${r.id}\` | ${r.addr} | ${r.range} | ${r.midi.join('<br>') || '—'} |\n`; }
    saveFile('Vektur MIDI reference.md', md, 'text/markdown'); });
  // Performance settings (About screen) and recording depth.
  const bs = store.get('batterySaver', false);
  $('#s-about').insertAdjacentHTML('afterbegin', `<div class="sheet"><h3>performance</h3>
    <div class="row"><label class="tfield">oscillators <select id="setOsc"><option value="0">standard</option><option value="1">HQ (2× oversampled)</option></select></label>
    <label class="tfield">filter oversampling <select id="setOs"><option value="0">1×</option><option value="1">2×</option><option value="2">4×</option></select></label>
    <label class="tfield"><input type="checkbox" id="setBattery" ${bs ? 'checked' : ''}> battery saving mode</label>
    <label class="tfield">recording <select id="setBits"><option value="16">16-bit WAV</option><option value="24">24-bit WAV</option><option value="32">32-bit float WAV</option></select></label></div>
    <p class="note">HQ oscillators cut aliasing on high notes (roughly a quarter more engine CPU). Battery Saving halves the cable animation rate, stops decorative motion and uses standard oscillators with 1× filters; turning it off restores your settings.</p></div>`);
  const osc = P['global.oscillatorQuality'], os = P['global.oversampling'];
  const sync = () => { $('#setOsc').value = String(values.get(osc.a) ?? osc.d); $('#setOs').value = String(values.get(os.a) ?? os.d); $('#setOsc').disabled = $('#setOs').disabled = store.get('batterySaver', false); $('#setBits').value = String(store.get('recBits', 24)); };
  $('#setOsc').addEventListener('change', (e) => { setParam(osc.a, +e.target.value); saveSession(); });
  $('#setOs').addEventListener('change', (e) => { setParam(os.a, +e.target.value); saveSession(); });
  $('#setBits').addEventListener('change', (e) => store.set('recBits', +e.target.value));
  $('#setBattery').addEventListener('change', (e) => {
    const on = e.target.checked;
    if (on) { store.set('bsSaved', [values.get(osc.a) ?? osc.d, values.get(os.a) ?? os.d]); setParam(osc.a, 0); setParam(os.a, 0); }
    else { const [q, o] = store.get('bsSaved', [osc.d, os.d]); setParam(osc.a, q); setParam(os.a, o); }
    store.set('batterySaver', on); reduceMotion = systemReduceMotion || on; saveSession(); sync();
    toast(on ? 'Battery Saving on' : 'Battery Saving off — settings restored');
  });
  sync();
}
}
