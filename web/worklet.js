// worklet.js — AudioWorklet host for the Vektur kernel (see engine.js).
import { Engine } from './engine.js';

class VekturProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { bytes } = options.processorOptions;
    this.engine = new Engine(bytes, sampleRate, (m) => this.port.postMessage(m));
    this.port.onmessage = (e) => this.engine.message(e.data);
  }
  process(_inputs, outputs) {
    const out = outputs[0];
    this.engine.render(out[0], out[1]);
    return true;
  }
}
registerProcessor('vektur', VekturProcessor);
