// engine.js — the Vektur kernel (kernel.wasm, the same C++ as the app and
// the AUv3) driven from JavaScript. Used inside the AudioWorklet
// (worklet.js) and, where AudioWorklet is unavailable, on the main thread
// through a ScriptProcessorNode. All messages are plain objects.
//
// UI → engine:  {t:'set',a,v} {t:'midi',d:[..]} {t:'ump',w0,w1} {t:'cmd',type,arg,value}
//               {t:'fxtype',slot,type} {t:'patch',cables:[[id,sm,sp,dm,dp,gain]]}
//               {t:'maps',list:[...]} {t:'learn',on} {t:'transport',playing,tempo,num,den,beat?}
//               {t:'preset',seed,cat} {t:'values',addrs:[..]}
// engine → UI:  {t:'ready'} {t:'status',...} {t:'values',pairs} {t:'midiout',ev:[[offsetMs,b0,b1,b2]]}
export function wasmEnv(getMem, log) {
  // AudioWorkletGlobalScope has no TextDecoder: decode ASCII by hand there.
  const decode = (u8) => (typeof TextDecoder !== 'undefined' ? new TextDecoder().decode(u8) : String.fromCharCode(...u8));
  return {
    js_sin: Math.sin, js_cos: Math.cos, js_tan: Math.tan, js_exp: Math.exp, js_log: Math.log,
    js_pow: Math.pow, js_atan2: Math.atan2, js_tanh: Math.tanh, js_cosh: Math.cosh,
    js_now: () => (globalThis.performance ? performance.now() : Date.now()),
    js_abort: (c) => { throw new Error('kernel abort ' + c); },
    js_log_text: (p, n) => log && log(decode(new Uint8Array(getMem().buffer, p, n).slice())),
  };
}

export function readLayout(x) {
  const L = Array.from(new Int32Array(x.memory.buffer, x.vkw_layout(), x.vkw_layout_count()));
  let i = 0; const n = () => L[i++];
  return {
    ev: { size: n(), off: n(), type: n(), len: n(), data: n(), addr: n(), value: n(), ramp: n(), ump: n() },
    cable: { size: n(), id: n(), sm: n(), sp: n(), dm: n(), dp: n(), gain: n() },
    map: { size: n(), source: n(), channel: n(), number: n(), address: n(), min: n(), max: n(), mode: n(), relative: n(), pickup: n(), sens: n() },
    out: { size: n(), off: n(), len: n(), data: n() },
    ui: { kind: n(), source: n(), channel: n(), msb: n(), lsb: n(), number: n() },
    ms: { tempo: n(), running: n(), active: n(), sync: n(), inN: n(), outN: n(), last: n() },
    seq: { step: n(), arp: n(), bar: n(), phrase: n(), flags: n(), rows: n(), beat: n() },
    tel: { peak: n(), rms: n(), voices: n(), nanos: n(), flags: n() },
    mod: { key: n(), name: n(), param: n(), ins: n(), outs: n() },
    port: { key: n(), name: n(), kind: n(), att: n() },
    channels: n(),
  };
}

export class Engine {
  constructor(bytes, sampleRate, post) {
    this.post = post; this.sr = sampleRate; this.ready = false; this.queue = [];
    this.transport = { playing: false, tempo: 120, num: 4, den: 4, beat: 0 };
    this.statusEvery = Math.max(1, Math.round(sampleRate / 128 / 30));
    this.blocks = 0; this.peak = [0, 0]; this.nanos = 0; this.voices = 0; this.flags = 0; this.fxDirty = false;
    this.learnPending = false;
    WebAssembly.instantiate(bytes, { env: wasmEnv(() => this.x.memory, (s) => post({ t: 'log', s })) }).then(({ instance }) => {
      this.x = instance.exports; this.L = readLayout(this.x);
      const r = this.x.vkw_init(sampleRate);
      this.ready = r === 0;
      post({ t: 'ready', ok: this.ready, code: r });
    }).catch((e) => post({ t: 'ready', ok: false, error: String(e) }));
  }

  u8() { return new Uint8Array(this.x.memory.buffer); }
  dv() { return new DataView(this.x.memory.buffer); }

  message(m) {
    if (!this.ready) { this.queue.push(m); return; }
    const x = this.x;
    switch (m.t) {
      case 'set': x.vkw_set(m.a, m.v); if (m.a >= 39 * 256 && m.a < 48 * 256) this.fxDirty = true; break;
      case 'midi': case 'ump': this.queue.push(m); break;
      case 'cmd': x.vkw_command(m.type, m.arg || 0, m.value || 0); break;
      case 'fxtype': x.vkw_set_effect_type(m.slot, m.type); this.sendValues(Array.from({ length: 17 }, (_, i) => (39 + m.slot) * 256 + i)); break;
      case 'patch': {
        const C = this.L.cable, base = x.vkw_cables(), dv = this.dv();
        m.cables.forEach((c, i) => {
          const o = base + i * C.size;
          dv.setUint16(o + C.id, c[0], true); dv.setUint8(o + C.sm, c[1]); dv.setUint8(o + C.sp, c[2]);
          dv.setUint8(o + C.dm, c[3]); dv.setUint8(o + C.dp, c[4]); dv.setFloat32(o + C.gain, c[5], true);
        });
        const rejected = x.vkw_set_patch(m.cables.length);
        this.post({ t: 'patched', rejected, n: m.cables.length });
        break;
      }
      case 'maps': {
        const M = this.L.map, base = x.vkw_mappings(), dv = this.dv();
        m.list.slice(0, 256).forEach((q, i) => {
          const o = base + i * M.size;
          new Uint8Array(x.memory.buffer, o, M.size).fill(0);
          dv.setUint8(o + M.source, q.source); dv.setUint8(o + M.channel, q.channel); dv.setUint16(o + M.number, q.number, true);
          dv.setBigUint64(o + M.address, BigInt(q.address), true); dv.setFloat32(o + M.min, q.min, true); dv.setFloat32(o + M.max, q.max, true);
          dv.setUint8(o + M.mode, q.mode); dv.setUint8(o + M.relative, q.relative); dv.setUint8(o + M.pickup, q.pickup ? 1 : 0);
          dv.setFloat32(o + M.sens, q.sens || 1 / 128, true);
        });
        x.vkw_publish_mappings(Math.min(256, m.list.length));
        break;
      }
      case 'learn': x.vkw_midi_learn(m.on ? 1 : 0); break;
      case 'transport': {
        const T = this.transport, wasPlaying = T.playing;
        Object.assign(T, m);
        if (m.playing && !wasPlaying && m.beat === undefined) T.beat = 0;
        break;
      }
      case 'preset': x.vkw_generate_preset(m.seed >>> 0, m.cat); this.sendAll(); this.fxDirty = true; break;
      case 'values': this.sendValues(m.addrs); break;
      case 'all': this.sendAll(); break;
      case 'addrs': this.addrs = m.list; break;
      case 'record': this.recording = !!m.on; this.recBuf = []; break;
    }
  }

  sendValues(addrs) { this.post({ t: 'values', pairs: addrs.map((a) => [a, this.x.vkw_get(a)]) }); }
  sendAll() { if (this.addrs) this.sendValues(this.addrs); }

  // Renders `frames` (≤ 512) into outL/outR (Float32Array).
  render(outL, outR) {
    const frames = outL.length;
    if (!this.ready) { outL.fill(0); outR && outR.fill(0); return; }
    const x = this.x;
    if (this.queue.length && this.queue.some((m) => m.t !== 'midi' && m.t !== 'ump')) {
      const q = this.queue; this.queue = [];
      for (const m of q) this.message(m);
    }
    if (this.fxDirty) { x.vkw_update_effects(); this.fxDirty = false; }
    // Events: everything queued lands at offset 0 of this quantum, in order.
    const E = this.L.ev, base = x.vkw_events(), max = x.vkw_max_events();
    let n = 0;
    const u8 = this.u8(), dv = this.dv();
    const q = this.queue; this.queue = [];
    for (const m of q) {
      if (n >= max) { this.queue.push(m); continue; }
      const o = base + n * E.size;
      u8.fill(0, o, o + E.size);
      if (m.t === 'midi') {
        dv.setUint8(o + E.type, 0); dv.setUint8(o + E.len, Math.min(3, m.d.length));
        for (let i = 0; i < Math.min(3, m.d.length); i++) dv.setUint8(o + E.data + i, m.d[i]);
      } else if (m.t === 'ump') {
        dv.setUint8(o + E.type, 3); dv.setUint32(o + E.ump, m.w0 >>> 0, true); dv.setUint32(o + E.ump + 4, m.w1 >>> 0, true);
      } else { this.message(m); continue; }
      n++;
    }
    // Standalone host transport (the web page is this unit's host).
    const T = this.transport;
    if (T.playing) {
      const bpb = T.num * 4 / T.den;
      x.vkw_transport(T.tempo, T.beat, 1, Math.floor(T.beat / bpb) * bpb, T.num, T.den);
      T.beat += frames / this.sr * T.tempo / 60;
    }
    x.vkw_render(frames, n);
    const mem = x.memory.buffer;
    outL.set(new Float32Array(mem, x.vkw_left(), frames));
    if (outR) outR.set(new Float32Array(mem, x.vkw_right(), frames));
    if (this.recording) {   // Phase 9: recording tap, sent to the page in ~85 ms chunks
      this.recBuf.push(outL.slice(), (outR || outL).slice());
      if (this.recBuf.length >= 64) { this.post({ t: 'audio', chunks: this.recBuf }); this.recBuf = []; }
    } else if (this.recBuf && this.recBuf.length) { this.post({ t: 'audio', chunks: this.recBuf, last: true }); this.recBuf = []; }

    // MIDI out → page (Web MIDI output, monitor).
    const op = this.op || (this.op = x.malloc(4));
    const no = x.vkw_midi_out(op);
    if (no) {
      const O = this.L.out, ptr = new DataView(mem).getUint32(op, true), ev = [];
      const d = new DataView(mem);
      for (let i = 0; i < no; i++) {
        const o = ptr + i * O.size, len = d.getUint8(o + O.len);
        ev.push([d.getUint32(o + O.off, true) / this.sr * 1000, ...Array.from({ length: len }, (_, k) => d.getUint8(o + O.data + k))]);
      }
      this.post({ t: 'midiout', ev });
    }
    // Telemetry → meters.
    for (let tp; (tp = x.vkw_telemetry());) {
      const T2 = this.L.tel, d = new DataView(x.memory.buffer);
      this.peak[0] = Math.max(this.peak[0], d.getFloat32(tp + T2.peak, true));
      this.peak[1] = Math.max(this.peak[1], d.getFloat32(tp + T2.peak + 4, true));
      this.voices = d.getUint32(tp + T2.voices, true);
      this.nanos = Math.max(this.nanos, Number(d.getBigUint64(tp + T2.nanos, true)));
      this.flags |= d.getUint32(tp + T2.flags, true);
    }
    if (++this.blocks % this.statusEvery === 0) this.status(frames);
  }

  status(frames) {
    const x = this.x, d = new DataView(x.memory.buffer);
    const S = this.L.seq, sp = x.vkw_seq_status();
    const MS = this.L.ms, mp = x.vkw_midi_status();
    const ui = [];
    for (let p; (p = x.vkw_midi_ui());) {
      const U = this.L.ui;
      ui.push({ kind: d.getUint8(p + U.kind), source: d.getUint8(p + U.source), channel: d.getUint8(p + U.channel),
        msb: d.getUint8(p + U.msb), lsb: d.getUint8(p + U.lsb), number: d.getUint16(p + U.number, true) });
    }
    let changed = [];
    if (ui.some((e) => e.kind === 2)) {
      const buf = this.cbuf || (this.cbuf = x.malloc(8 * 512));
      const n = x.vkw_take_changes(buf, 512);
      const dv = new DataView(x.memory.buffer);
      for (let i = 0; i < n; i++) { const a = Number(dv.getBigUint64(buf + i * 8, true)); changed.push([a, x.vkw_get(a)]); }
    }
    const held = this.hb || (this.hb = x.malloc(16));
    x.vkw_held(held);
    const hw = Array.from(new Uint32Array(x.memory.buffer, held, 4));
    const fx = []; for (let s = 0; s < 8; s++) fx.push(x.vkw_effect_meter(s));
    const cables = []; for (let c = 0; c < 64; c++) cables.push(x.vkw_cable_level(c));
    const mods = []; for (let s = 0; s < 16; s++) mods.push(x.vkw_mod_slot(s));
    const blockMs = frames / this.sr * 1000;
    this.post({ t: 'status',
      peak: this.peak.slice(), voices: this.voices, load: globalThis.performance ? this.nanos / 1e6 / blockMs : null, flags: this.flags,
      seq: { step: d.getInt32(sp + S.step, true), arp: d.getInt32(sp + S.arp, true), bar: d.getInt32(sp + S.bar, true),
        phrase: d.getInt32(sp + S.phrase, true), flags: d.getUint32(sp + S.flags, true), beat: d.getFloat64(sp + S.beat, true),
        rows: [0, 1, 2].map((k) => d.getFloat32(sp + S.rows + 4 * k, true)) },
      midi: { tempo: d.getFloat64(mp + MS.tempo, true), running: !!d.getUint8(mp + MS.running), active: !!d.getUint8(mp + MS.active),
        sync: d.getUint8(mp + MS.sync), inN: d.getUint32(mp + MS.inN, true), outN: d.getUint32(mp + MS.outN, true),
        last: [0, 1, 2].map((k) => d.getUint8(mp + MS.last + k)) },
      ui, changed, held: hw, fx, cables, mods, latency: x.vkw_effects_latency(), beat: this.transport.beat });
    this.peak = [0, 0]; this.nanos = 0; this.flags = 0;
  }
}
