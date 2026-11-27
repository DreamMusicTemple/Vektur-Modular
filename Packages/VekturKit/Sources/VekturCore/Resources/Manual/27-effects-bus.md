# 27. Effects routing: inserts, sends and returns

Each slot is assigned to one of three buses: the **insert** chain, **Send A** or **Send B**. Each bus runs its slots in series or in parallel. The sends tap the signal before or after the inserts, and each return has its own level. This lets one reverb be shared by several sounds, or a delay feed a reverb.

**Effects On** bypasses the whole section.

| Control | Range | Default | Identifier |
|---|---|---|---|
| Effects On | 0 … 1 | 1 | `fxBus.enabled` |
| Insert Chain | Series / Parallel | Series | `fxBus.insertMode` |
| Send A Chain | Series / Parallel | Series | `fxBus.sendAMode` |
| Send B Chain | Series / Parallel | Series | `fxBus.sendBMode` |
| Send A Level | 0 % … 100 % | 30 % | `fxBus.sendA` |
| Send B Level | 0 % … 100 % | 30 % | `fxBus.sendB` |
| Send A Tap | Pre Inserts / Post Inserts | Post Inserts | `fxBus.sendAPosition` |
| Send B Tap | Pre Inserts / Post Inserts | Post Inserts | `fxBus.sendBPosition` |
| Return A Level | -60 dB … 6 dB | 0 dB | `fxBus.returnA` |
| Return B Level | -60 dB … 6 dB | 0 dB | `fxBus.returnB` |
| Insert Chain Level | -60 dB … 6 dB | 0 dB | `fxBus.dry` |
| Effects Output | -24 dB … 12 dB | 0 dB | `fxBus.output` |
