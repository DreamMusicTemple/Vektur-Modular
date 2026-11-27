// VekturDSP.h — public C interface to the Vektur Modular C++ DSP kernel.
//
// This header is deliberately plain C so Swift can import it without C++
// interop flags. Everything behind it is C++20.
//
// THREADING CONTRACT (read before calling anything):
//   [any]      callable from any thread, lock-free, never blocks.
//   [control]  non-real-time thread only (main / setup). May allocate.
//              Must NOT run concurrently with [render] calls.
//   [render]   the audio render thread only. Never allocates, locks,
//              logs or touches files.
//   [ui]       a single consumer/producer thread (normally the main thread).

#ifndef VEKTUR_DSP_H
#define VEKTUR_DSP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Limits (fixed so every buffer can be preallocated)
// ---------------------------------------------------------------------------
#define VK_MAX_CHANNELS          2u
#define VK_MAX_VOICES            32u
#define VK_MAX_EVENTS_PER_RENDER 1024u
#define VK_CONTROL_BLOCK         32u      // control-rate sub-block, in frames
// Parameter address space: address = module * VK_PARAMS_PER_MODULE + index.
#define VK_MAX_MODULES           64u
#define VK_PARAMS_PER_MODULE     256u
#define VK_ADDRESS_SPACE         (VK_MAX_MODULES * VK_PARAMS_PER_MODULE)
#define VK_MAX_REGISTERED_PARAMS 2048u

// ---------------------------------------------------------------------------
// Stable parameter addresses live in VekturParams.h, generated from
// tools/params.py (the single source of truth for Swift and C++).
// NEVER renumber or reuse an address once it has shipped.
// ---------------------------------------------------------------------------
#include "VekturParams.h"

// 1, 2, 4, 6, 8, 12, 16, 24, 32 voices (index = VK_P_GLOBAL_POLYPHONY value)
#define VK_POLYPHONY_CHOICES             9u

typedef struct VKKernel VKKernel;

// ---------------------------------------------------------------------------
// Events delivered to render (sample-accurate)
// ---------------------------------------------------------------------------
typedef enum VKEventType {
    VK_EVENT_MIDI1     = 0,   // data[0..len) = MIDI 1.0 bytes
    VK_EVENT_PARAM     = 1,   // address/value, rampFrames = 0 → default smoothing
    VK_EVENT_PARAM_RAMP= 2,   // linear ramp over rampFrames
    VK_EVENT_UMP       = 3    // Phase 7: one Universal MIDI Packet (MIDI 2.0 channel voice), words in ump[]
} VKEventType;

typedef struct VKEvent {
    uint32_t sampleOffset;    // frames from start of this render call
    uint8_t  type;            // VKEventType
    uint8_t  length;          // MIDI length (1..3)
    uint8_t  data[3];
    uint64_t address;
    float    value;
    uint32_t rampFrames;
    uint32_t ump[2];          // VK_EVENT_UMP: 64-bit MIDI 2.0 message
} VKEvent;

// ---------------------------------------------------------------------------
// UI → audio commands (single producer: the [ui] thread)
// ---------------------------------------------------------------------------
typedef enum VKCommandType {
    VK_CMD_ALL_NOTES_OFF = 0, // releases every voice (with envelopes)
    VK_CMD_PANIC         = 1, // hard reset: voices silenced with a short fade
    VK_CMD_RESET_METERS  = 2,
    VK_CMD_CAPTURE       = 3  // Phase 5: arg0 = 1 starts recording played notes, 0 stops
} VKCommandType;

typedef struct VKCommand {
    uint32_t type;
    uint32_t arg0;
    float    value;
} VKCommand;

// ---------------------------------------------------------------------------
// Audio → UI telemetry (single consumer: the [ui] thread)
// ---------------------------------------------------------------------------
typedef struct VKTelemetry {
    float    peak[VK_MAX_CHANNELS];
    float    rms[VK_MAX_CHANNELS];
    uint32_t activeVoices;
    uint32_t frames;
    uint64_t renderNanos;      // wall time spent inside render
    uint64_t sampleTime;       // running frame counter
    uint32_t flags;            // VK_TELEMETRY_FLAG_*
} VKTelemetry;

#define VK_TELEMETRY_FLAG_CLIPPED     0x1u  // output safety clamp engaged
#define VK_TELEMETRY_FLAG_NONFINITE   0x2u  // NaN/Inf detected and recovered
#define VK_TELEMETRY_FLAG_EVENTS_DROP 0x4u  // event list exceeded capacity

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
VKKernel* vk_kernel_create(void);                                  // [control]
void      vk_kernel_destroy(VKKernel* k);                          // [control]

// Registers a parameter so the kernel smooths it. Call before allocate.
// Returns false for out-of-range addresses or a full registry.
bool vk_kernel_register_param(VKKernel* k, uint64_t address,
                              float minValue, float maxValue,
                              float defaultValue, float smoothingMs); // [control]

// Allocates every buffer the render path will use. Returns 0 on success.
int  vk_kernel_allocate(VKKernel* k, double sampleRate,
                        uint32_t channelCount, uint32_t maxFrames); // [control]
void vk_kernel_deallocate(VKKernel* k);                             // [control]
bool vk_kernel_is_allocated(const VKKernel* k);                     // [any]

// ---------------------------------------------------------------------------
// Parameters (lock-free; values are clamped to the registered range)
// ---------------------------------------------------------------------------
void  vk_kernel_set_param(VKKernel* k, uint64_t address, float value); // [any]
float vk_kernel_get_param(const VKKernel* k, uint64_t address);        // [any]

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
// channels: channelCount non-interleaved float buffers of `frames` frames.
// events must be sorted by sampleOffset (AU event lists already are).
void vk_kernel_render(VKKernel* k, float* const* channels, uint32_t channelCount,
                      uint32_t frames, const VKEvent* events,
                      uint32_t eventCount);                            // [render]

// ---------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------
bool vk_kernel_post_command(VKKernel* k, VKCommand cmd);              // [ui]
bool vk_kernel_pop_telemetry(VKKernel* k, VKTelemetry* out);          // [ui]

// ---------------------------------------------------------------------------
// AUv3 adapter (Apple platforms only; implemented in AURenderAdapter.mm).
// Walks the host's AURenderEvent list, converts it to VKEvents in a
// preallocated array and renders into the host's AudioBufferList. The Swift
// internalRenderBlock forwards straight to this, so no Swift code (and no
// ARC traffic) runs per event on the render thread.
//
// Types are passed as raw pointers so this header stays plain C and does not
// need the Objective-C AUAudioUnit headers:
//   actionFlags            -> AudioUnitRenderActionFlags*
//   outputData             -> AudioBufferList*
//   realtimeEventListHead  -> const AURenderEvent* (may be NULL)
// Returns an OSStatus (noErr == 0).
// ---------------------------------------------------------------------------
int32_t vk_au_render(VKKernel* k,
                     uint32_t* actionFlags,
                     double timestampSampleTime,
                     uint32_t frameCount,
                     void* outputData,
                     const void* realtimeEventListHead);              // [render]

// ---------------------------------------------------------------------------
// Modular rack & patching (Phase 3)
//
// The rack is a fixed set of module instances (like a hardware cabinet). The
// default (normalled) connections reproduce the semi-modular voice; a user
// cable into an input replaces that input's normalled connection. Every
// module instance is per-voice. Introspection functions are [any].
// ---------------------------------------------------------------------------
#define VK_MAX_CABLES        128u   // user cables per patch
#define VK_MAX_CABLE_ID      256u   // cable ids are 0 … 255
#define VK_NORMAL_SLOT_BASE  256u   // telemetry slot of normalled edge i = BASE + i
#define VK_TELEMETRY_SLOTS   512u

typedef enum VKPortKind { VK_PORT_AUDIO = 0, VK_PORT_CV = 1, VK_PORT_GATE = 2, VK_PORT_TRIGGER = 3 } VKPortKind;

typedef struct VKModuleInfo {
    const char* key;          // stable identifier, e.g. "vco4" (stored in saved patches)
    const char* name;         // display name
    int32_t paramModule;      // parameter address block (address = block*256 + index), -1 = none
    uint8_t inputCount;
    uint8_t outputCount;
} VKModuleInfo;

typedef struct VKPortInfo {
    const char* key;          // stable identifier, e.g. "fm"
    const char* name;
    uint8_t kind;             // VKPortKind
    int64_t attenuatorAddress;// input attenuator parameter, -1 = none
} VKPortInfo;

typedef struct VKNormalInfo { uint8_t srcModule, srcPort, dstModule, dstPort; } VKNormalInfo;

typedef struct VKCable {
    uint16_t id;              // 0 … VK_MAX_CABLE_ID-1, unique within a patch; telemetry slot
    uint8_t srcModule, srcPort;   // output
    uint8_t dstModule, dstPort;   // input
    float gain;               // per-cable attenuverter (-2 … 2), 1 = unity
} VKCable;

typedef enum VKRejectReason {
    VK_REJECT_NONE = 0, VK_REJECT_BAD_INDEX = 1, VK_REJECT_DIRECTION = 2, VK_REJECT_INPUT_TAKEN = 3,
    VK_REJECT_TOO_MANY = 4, VK_REJECT_DUPLICATE_ID = 5, VK_REJECT_BAD_GAIN = 6
} VKRejectReason;

typedef struct VKPatchResult {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t feedbackEdges;   // cycles broken with a one-block delay + limiter
    uint16_t rejectedIds[16];
    uint8_t rejectedReasons[16];
} VKPatchResult;

uint32_t vk_rack_module_count(void);                                                  // [any]
bool vk_rack_module_info(uint32_t module, VKModuleInfo* out);                         // [any]
bool vk_rack_port_info(uint32_t module, uint32_t port, bool input, VKPortInfo* out);  // [any]
uint32_t vk_rack_normal_count(void);                                                  // [any]
bool vk_rack_normal_info(uint32_t index, VKNormalInfo* out);                          // [any]

// Validates, compiles (topological order, cycle breaking) and publishes a new
// patch. The audio thread picks it up at the next block with a 1.3 ms
// fade-out/in. Returns 0 if every cable was accepted. [ui] — one caller thread.
int32_t vk_kernel_set_patch(VKKernel* k, const VKCable* cables, uint32_t count, VKPatchResult* result);

// Recent peak level carried by a connection (0 = silent). slot = cable id, or
// VK_NORMAL_SLOT_BASE + normal index. Updated ~every 4 control blocks. [any]
float vk_kernel_connection_level(const VKKernel* k, uint32_t slot);

// ---------------------------------------------------------------------------
// Phase 4B — performance
// ---------------------------------------------------------------------------
// [render] Host musical context for this render call (tempo-synced LFOs).
// Call immediately before vk_kernel_render / vk_au_render; without it the
// kernel uses the Internal Tempo parameter and a stopped transport.
void vk_kernel_set_transport(VKKernel* k, double tempoBPM, double beatPosition, bool playing);
// [ui] Live value of matrix slot 0…15 on the most recently played voice.
float vk_kernel_mod_slot_value(const VKKernel* k, uint32_t slot);
// [ui] Keys currently held, as a 128-bit mask (out[0] bit 0 = note 0).
void vk_kernel_held_notes(const VKKernel* k, uint32_t out[4]);
// [any] Chord name for a set of MIDI notes ("Cmaj7", "Am/C", "D5", "F#"). Returns
// the root pitch class (0…11) or -1 when no chord is recognised; `out` gets ""
// for an empty set. Pure function, allocation-free.
int32_t vk_chord_name(const uint8_t* notes, uint32_t count, char* out, uint32_t capacity);

// ---------------------------------------------------------------------------
// Phase 5 — arpeggiator, step sequencer, generative engine, preset creator
//
// The generators run inside render on the musical clock: the host's beat
// position while its transport moves, otherwise a free-running clock at the
// current tempo. Their notes are sample-accurate (spans are split at each
// note event). All settings are ordinary parameters (modules 36–38).
// ---------------------------------------------------------------------------
typedef enum VKNoteSource { VK_SOURCE_KEYS = 0, VK_SOURCE_ARP = 1, VK_SOURCE_SEQ = 2, VK_SOURCE_GEN = 3 } VKNoteSource;

// One note-on (velocity 1…127) or note-off (velocity 0) at a beat position.
typedef struct VKNoteRecord {
    double  beat;
    uint8_t note;
    uint8_t velocity;
    uint8_t source;           // VKNoteSource
    uint8_t reserved;
} VKNoteRecord;

#define VK_SEQ_FLAG_ARP_RUNNING  0x1u
#define VK_SEQ_FLAG_SEQ_RUNNING  0x2u
#define VK_SEQ_FLAG_GEN_RUNNING  0x4u
#define VK_SEQ_FLAG_HOST_PLAYING 0x8u
#define VK_SEQ_FLAG_CAPTURING    0x10u

typedef struct VKSequencerStatus {
    int32_t seqStep;          // 0…15, −1 when idle
    int32_t arpNote;          // last arp note, −1 = none
    int32_t genBar;           // bar within the current phrase, −1 when idle
    int32_t genPhrase;        // phrase (repeat) number, −1 when idle
    uint32_t flags;           // VK_SEQ_FLAG_*
    float rows[3];            // current step's rows A, B, C (0…1)
    double beat;              // generators' clock
} VKSequencerStatus;

// [ui] Notes played while capture is armed (VK_CMD_CAPTURE), in order.
bool vk_kernel_pop_note_record(VKKernel* k, VKNoteRecord* out);
// [any] Live state of the generators, for LEDs and readouts.
void vk_kernel_sequencer_status(const VKKernel* k, VKSequencerStatus* out);

// [control] Renders phrase `repeat` of the generative engine from the current
// parameters, exactly as the kernel would play it (swing, gate, evolve), as
// note records with beats relative to the phrase start. Returns the number of
// records written (≤ capacity), or with out = NULL the number available;
// *phraseBeats receives the phrase length.
uint32_t vk_kernel_generative_phrase(const VKKernel* k, uint32_t repeat, VKNoteRecord* out,
                                     uint32_t capacity, double* phraseBeats);
// [control] Chord name (e.g. "Am", "G7") of bar 0…7 of phrase `repeat`.
// Returns the bar count of the phrase.
uint32_t vk_kernel_generative_chords(const VKKernel* k, uint32_t repeat, char names[8][12]);

// [control] Standard MIDI File (format 0, 480 PPQ) from note records. Notes
// still open at the end are closed at the last event. Returns the file size;
// writes only when capacity is large enough (pass NULL/0 to query the size).
uint32_t vk_midi_file_write(const VKNoteRecord* records, uint32_t count, double tempoBPM,
                            uint8_t* out, uint32_t capacity);

// Generative preset creator. Categories:
typedef enum VKPresetCategory {
    VK_PRESET_BASS = 0, VK_PRESET_LEAD = 1, VK_PRESET_PAD = 2, VK_PRESET_PLUCK = 3,
    VK_PRESET_KEYS = 4, VK_PRESET_BRASS = 5, VK_PRESET_FX = 6, VK_PRESET_DRONE = 7,
    VK_PRESET_CATEGORY_COUNT = 8
} VKPresetCategory;

typedef struct VKParamValue { uint64_t address; float value; } VKParamValue;

typedef struct VKPresetReport {
    float rmsDb;              // audition level (held C3 + C4, 0.6 s)
    float peak;
    uint32_t attempts;        // seeds tried before one passed (1 = first)
    bool repaired;            // the safe fallback voice had to be used
} VKPresetReport;

// [control] Builds a complete sound for `category` from `seed`, constrained to
// musically useful ranges, then auditions it offline and retries (with a
// derived seed) until it is audible, finite and below clipping. Never returns
// a silent sound. Writes every synthesis parameter (performance settings —
// tempo, MPE, zones, polyphony, arp/sequencer/generative — are left out).
// Returns the number of values written (≤ capacity; 512 is always enough).
uint32_t vk_preset_generate(uint32_t seed, uint32_t category, VKParamValue* out,
                            uint32_t capacity, VKPresetReport* report);

// ---------------------------------------------------------------------------
// Phase 6 — effects
//
// Eight slots (parameter modules 39–46) on three buses: the insert chain,
// send A and send B (module 47). Each chain runs in series or in parallel;
// sends tap pre- or post-inserts. Every path is latency-compensated and the
// total is reported by vk_kernel_effects_latency (constant until the effect
// types or routing change). A slot's p1…p12 parameters (0…1) take their
// meaning from its effect type — use the catalogue functions to name,
// format and default them.
// ---------------------------------------------------------------------------
#define VK_FX_SLOTS       8u
#define VK_FX_PARAMS      12u

typedef enum VKFxUnit {
    VK_FX_UNIT_NONE = 0, VK_FX_UNIT_PERCENT, VK_FX_UNIT_MS, VK_FX_UNIT_HZ, VK_FX_UNIT_DB, VK_FX_UNIT_SEMITONES,
    VK_FX_UNIT_RATIO, VK_FX_UNIT_DEGREES, VK_FX_UNIT_BITS, VK_FX_UNIT_SECONDS, VK_FX_UNIT_PER_SECOND,
    VK_FX_UNIT_MULTIPLIER, VK_FX_UNIT_CHOICE, VK_FX_UNIT_TOGGLE
} VKFxUnit;

typedef struct VKFxTypeInfo {
    const char* name;         // "Hall Reverb"
    const char* family;       // "Reverb"
    float defaultMix;         // % set when the type is chosen
    uint32_t paramCount;      // highest used parameter index + 1
} VKFxTypeInfo;

typedef struct VKFxParamInfo {
    const char* name;         // NULL = unused for this type
    uint8_t unit;             // VKFxUnit
    bool isLog, isChoice, isToggle;
    bool structural;          // changing it rebuilds the effect (call vk_kernel_update_effects)
    float minValue, maxValue, defaultValue;   // real units
    float defaultNormalised;  // value to store in the slot parameter
    uint32_t choiceCount;
} VKFxParamInfo;

uint32_t vk_fx_type_count(void);                                                          // [any]
bool vk_fx_type_info(uint32_t type, VKFxTypeInfo* out);                                   // [any]
bool vk_fx_param_info(uint32_t type, uint32_t index, VKFxParamInfo* out);                 // [any] index 0…11
const char* vk_fx_choice_name(uint32_t type, uint32_t index, uint32_t choice);           // [any]
float vk_fx_map_value(uint32_t type, uint32_t index, float normalised);                  // [any] → real units
float vk_fx_unmap_value(uint32_t type, uint32_t index, float value);                     // [any] real units → 0…1
int32_t vk_fx_format_value(uint32_t type, uint32_t index, float normalised, char* out, uint32_t capacity); // [any]

// [ui] Rebuilds the slots whose effect type, structural setting or user IR
// changed since the last call (allocating on the calling thread) and hands
// them to the audio thread, which crossfades each slot (≈3 ms). Also frees
// replaced instances. Returns the number of slots rebuilt.
uint32_t vk_kernel_update_effects(VKKernel* k);
// [ui] Sets a slot's type with its defaults (mix and p1…p12), then updates.
void vk_kernel_set_effect_type(VKKernel* k, uint32_t slot, uint32_t type);
// [ui] User impulse response for a slot's convolution reverb ("User IR").
// Resampled to the session rate, truncated to 3 s; right may be NULL (mono).
void vk_kernel_set_effect_ir(VKKernel* k, uint32_t slot, const float* left, const float* right,
                             uint32_t frames, double sampleRate);
void vk_kernel_clear_effect_ir(VKKernel* k, uint32_t slot);
uint32_t vk_kernel_effect_ir_frames(const VKKernel* k, uint32_t slot);   // [ui] 0 = none
// [any] Total effects latency in samples (all paths compensated to this).
uint32_t vk_kernel_effects_latency(const VKKernel* k);
// [any] Type actually running in a slot (after any pending swap).
uint32_t vk_kernel_effect_active_type(const VKKernel* k, uint32_t slot);
// [any] Slot meter: gain reduction in dB (≤ 0) for dynamics, else 0.
float vk_kernel_effect_meter(const VKKernel* k, uint32_t slot);

// ---------------------------------------------------------------------------
// Phase 7 — MIDI engine
//
// Input: MIDI 1.0 bytes and MIDI 2.0 UMP (VK_EVENT_UMP). Handled in the
// kernel, sample-accurately: notes (with velocity curve and receive-channel
// filter), RPN (0 bend range, 1 fine tune, 6 MPE configuration) and NRPN,
// 14-bit CC, bank select + program change (reported to the UI), MIDI clock /
// start / continue / stop / song position (sync source), MIDI 2.0 32-bit
// controllers, high-resolution velocity, per-note pitch bend, per-note
// timbre (registered/assignable per-note controller 74) and per-note
// management. MIDI Learn mappings drive any parameter.
// Output: generated (or all) notes and MIDI clock, with sample offsets.
// ---------------------------------------------------------------------------
#define VK_MIDI_MAX_MAPPINGS 256u
#define VK_MIDI_OUT_MAX      512u
#define VK_MIDI_ANY_CHANNEL  255u

typedef enum VKMidiSourceKind {
    VK_MIDI_SRC_CC = 0,           // 7-bit (or 32-bit from MIDI 2.0) controller, number 0…127
    VK_MIDI_SRC_CC14 = 1,         // 14-bit controller pair, number = MSB controller 0…31
    VK_MIDI_SRC_NRPN = 2,         // number 0…16383, 14-bit (or 32-bit) data
    VK_MIDI_SRC_PITCH_BEND = 3,
    VK_MIDI_SRC_CHANNEL_PRESSURE = 4,
    VK_MIDI_SRC_NOTE = 5          // note number: gate (on = max, off = min) or toggle
} VKMidiSourceKind;

typedef enum VKMidiMapMode { VK_MIDI_MODE_ABSOLUTE = 0, VK_MIDI_MODE_TOGGLE = 1, VK_MIDI_MODE_RELATIVE = 2 } VKMidiMapMode;
typedef enum VKMidiRelative {
    VK_MIDI_REL_TWOS_COMPLEMENT = 0,   // 1…63 up, 127…65 down
    VK_MIDI_REL_BINARY_OFFSET = 1,     // 65… up, …63 down (64 = none)
    VK_MIDI_REL_SIGNED_BIT = 2         // 1…63 up, 65…127 down (bit 6 = sign)
} VKMidiRelative;

typedef struct VKMidiMapping {
    uint8_t  source;        // VKMidiSourceKind
    uint8_t  channel;       // 0…15, VK_MIDI_ANY_CHANNEL
    uint16_t number;
    uint64_t address;       // target parameter
    float    minimum;       // normalised (0…1, follows the parameter's taper); minimum > maximum inverts
    float    maximum;
    uint8_t  mode;          // VKMidiMapMode
    uint8_t  relative;      // VKMidiRelative (relative mode)
    bool     pickup;        // absolute mode: ignore the controller until it reaches the current value
    float    sensitivity;   // relative mode: normalised change per step (e.g. 1/128)
} VKMidiMapping;

typedef struct VKMidiOutEvent { uint32_t sampleOffset; uint8_t length; uint8_t data[3]; } VKMidiOutEvent;

typedef enum VKMidiUIEventKind {
    VK_MIDI_UI_LEARNED = 0,        // a source arrived while learning: source, channel, number
    VK_MIDI_UI_PROGRAM = 1,        // program change: channel, number = program, bankMSB/bankLSB
    VK_MIDI_UI_PARAMS_CHANGED = 2  // MIDI (learn / RPN / MPE configuration) changed parameters: read vk_kernel_take_midi_changes
} VKMidiUIEventKind;

typedef struct VKMidiUIEvent { uint8_t kind, source, channel, bankMSB, bankLSB; uint16_t number; } VKMidiUIEvent;

typedef struct VKMidiStatus {
    double clockTempo;      // BPM measured from incoming MIDI clock (0 = none)
    bool   clockRunning;    // start/continue received and ticks arriving
    bool   clockActive;     // a clock tick arrived within the last 0.5 s
    uint8_t syncSource;     // in effect: 0 internal, 1 host, 2 MIDI clock
    uint32_t messagesIn;    // running counters (wrap)
    uint32_t messagesOut;
    uint8_t lastStatus, lastData1, lastData2;   // most recent channel message (monitor)
} VKMidiStatus;

// [ui] Replaces the MIDI Learn table (published lock-free; per-mapping pickup state resets).
void vk_kernel_set_midi_mappings(VKKernel* k, const VKMidiMapping* mappings, uint32_t count);
// [ui] While armed, the next mappable message is reported as VK_MIDI_UI_LEARNED (it still plays).
void vk_kernel_midi_learn(VKKernel* k, bool armed);
bool vk_kernel_pop_midi_ui_event(VKKernel* k, VKMidiUIEvent* out);              // [ui] single consumer; out NULL discards
// [ui] Parameters MIDI has changed since the last call (for the host's parameter tree). Returns count.
uint32_t vk_kernel_take_midi_changes(VKKernel* k, uint64_t* addresses, uint32_t capacity);
void vk_kernel_midi_status(const VKKernel* k, VKMidiStatus* out);               // [any]
// [render] MIDI produced by the last render call (valid until the next render).
uint32_t vk_kernel_midi_output(const VKKernel* k, const VKMidiOutEvent** events);
// Apple: the AU's MIDI output block (AUMIDIOutputEventBlock), called by vk_au_render
// on the render thread after each buffer. Pass NULL to clear. [control]
void vk_au_set_midi_output_block(VKKernel* k, const void* block);

// [render] Host musical context with meter (Phase 7). barStartBeat = beat position of the
// current bar's downbeat; numerator/denominator of the time signature.
void vk_kernel_set_transport_ex(VKKernel* k, double tempoBPM, double beatPosition, bool playing,
                                double barStartBeat, double numerator, double denominator);

// ---------------------------------------------------------------------------
// Recording (Phase 9): a wait-free tap of the final output. Start/stop/read
// are [ui] (one reader); the render thread writes. Interleaved stereo float
// frames (mono output is duplicated). Drain at least every ~5 s at 96 kHz.
// ---------------------------------------------------------------------------
bool     vk_kernel_record_start(VKKernel* k);                                   // [ui] false if memory is unavailable
void     vk_kernel_record_stop(VKKernel* k);                                    // [ui]
bool     vk_kernel_is_recording(const VKKernel* k);                             // [any]
uint32_t vk_kernel_record_read(VKKernel* k, float* interleaved, uint32_t maxFrames); // [ui] returns frames
uint64_t vk_kernel_record_dropped(const VKKernel* k);                           // [any] frames lost to a slow reader

/// Phase 10: FFT self-test (vDSP on Apple, radix-2 elsewhere). Returns the
/// largest error of a forward and a round-trip transform of size n against a
/// direct DFT, relative to the signal peak; < 1e-4 is a pass. Allocates.
float vk_fft_selftest(uint32_t n);

#ifdef __cplusplus
}
#endif

#endif // VEKTUR_DSP_H
