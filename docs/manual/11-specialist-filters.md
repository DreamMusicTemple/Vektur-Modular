# 11. Specialist filters

Seven extra filters are available as patchable modules. None of them is connected by default:

- **Diode ladder.** A squelchy filter in the style of classic bass machines.
- **Transistor ladder.** A second ladder filter with two CV inputs.
- **OTA.** A state-variable filter with low-pass, band-pass and high-pass modes.
- **VCBPF.** A voltage-controlled band-pass filter with Q from 0.5 to 40.
- **Comb.** A tuned comb filter. Positive feedback gives metallic sounds; negative feedback gives hollow ones.
- **Formant.** Sweeps between vowels.
- **Filter bank.** A fixed eight-band bank, 125 Hz to 12 kHz.

Patch the voice mixer, or any oscillator, into a filter's input and patch its output into the VCA's audio input.

| Control | Range | Default | Identifier |
|---|---|---|---|
| OTA Mode | Low-pass / Band-pass / High-pass | Low-pass | `ota.mode` |
| OTA Cutoff | 20 Hz … 20000 Hz | 1000 Hz | `ota.cutoff` |
| OTA Resonance | 0 % … 100 % | 30 % | `ota.resonance` |
| OTA Drive | 0 dB … 24 dB | 6 dB | `ota.drive` |
| OTA CV 1 Amount | -10 oct … 10 oct | 5 oct | `ota.cv1Amount` |
| OTA CV 2 Amount | -10 oct … 10 oct | 0 oct | `ota.cv2Amount` |
| Comb Frequency | 20 Hz … 5000 Hz | 220 Hz | `comb.frequency` |
| Comb Feedback | -99 % … 99 % | 70 % | `comb.feedback` |
| Comb Damping | 0 % … 100 % | 20 % | `comb.damping` |
| Comb Mix | 0 % … 100 % | 100 % | `comb.mix` |
| Comb CV Amount | -10 oct … 10 oct | 5 oct | `comb.cvAmount` |
| Formant Vowel | 0 % … 100 % | 0 % | `formant.vowel` |
| Formant Resonance | 0 % … 100 % | 60 % | `formant.resonance` |
| Formant Shift | -12 st … 12 st | 0 st | `formant.shift` |
| Formant Mix | 0 % … 100 % | 100 % | `formant.mix` |
| Formant CV Amount | -100 % … 100 % | 100 % | `formant.cvAmount` |
| Bank 125 Hz | 0 % … 100 % | 100 % | `filterBank.band1` |
| Bank 250 Hz | 0 % … 100 % | 100 % | `filterBank.band2` |
| Bank 500 Hz | 0 % … 100 % | 100 % | `filterBank.band3` |
| Bank 1 kHz | 0 % … 100 % | 100 % | `filterBank.band4` |
| Bank 2 kHz | 0 % … 100 % | 100 % | `filterBank.band5` |
| Bank 4 kHz | 0 % … 100 % | 100 % | `filterBank.band6` |
| Bank 8 kHz | 0 % … 100 % | 100 % | `filterBank.band7` |
| Bank 12 kHz | 0 % … 100 % | 100 % | `filterBank.band8` |
| Bank Level | 0 % … 200 % | 100 % | `filterBank.level` |
| VCBPF Centre | 20 Hz … 20000 Hz | 1000 Hz | `vcbpf.centre` |
| VCBPF Q | 0.5 … 40 | 4 | `vcbpf.q` |
| VCBPF CV 1 Amount | -10 oct … 10 oct | 5 oct | `vcbpf.cv1Amount` |
| VCBPF CV 2 Amount | -10 oct … 10 oct | 0 oct | `vcbpf.cv2Amount` |
| VCBPF Level | 0 % … 200 % | 100 % | `vcbpf.level` |
| Diode Cutoff | 20 Hz … 20000 Hz | 800 Hz | `diodeLadder.cutoff` |
| Diode Resonance | 0 % … 100 % | 40 % | `diodeLadder.resonance` |
| Diode Drive | 0 dB … 24 dB | 3 dB | `diodeLadder.drive` |
| Diode CV 1 Amount | -10 oct … 10 oct | 5 oct | `diodeLadder.cv1Amount` |
| Diode CV 2 Amount | -10 oct … 10 oct | 0 oct | `diodeLadder.cv2Amount` |
| Transistor Cutoff | 20 Hz … 20000 Hz | 800 Hz | `transistorLadder.cutoff` |
| Transistor Resonance | 0 % … 100 % | 40 % | `transistorLadder.resonance` |
| Transistor Drive | 0 dB … 24 dB | 6 dB | `transistorLadder.drive` |
| Transistor CV 1 Amount | -10 oct … 10 oct | 5 oct | `transistorLadder.cv1Amount` |
| Transistor CV 2 Amount | -10 oct … 10 oct | 0 oct | `transistorLadder.cv2Amount` |
