#ifndef MUSYX_PC_H
#define MUSYX_PC_H

#include "musyx/musyx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single-instance PC renderer. Configure before sndInit; configuration persists
 * across sndQuit. sndInit never opens a device or creates a thread.
 * Supported rates: 8000 through 96000 Hz inclusive (integer Hz).
 * Interleaved signed 16-bit PCM. Channels select the speaker layout:
 * 2: FL FR; 4: FL FR BL BR; 6: FL FR FC LFE BL BR;
 * 8: FL FR FC LFE BL BR SL SR. Center/LFE are silent; in 7.1 the rear
 * directions split equally between back/side at -3 dB each.
 * Legacy effects process three channels at 32000 Hz. */
typedef struct SND_PC_CONFIG {
  u32 mixRate;
  u32 channels;
} SND_PC_CONFIG;

typedef struct SND_PC_RENDER_INFO {
  u64 outputFrames;
  u64 renderedFrames;
  u64 controlTicks;
  u32 mixRate;
  u32 channels;
} SND_PC_RENDER_INFO;

bool sndPCConfigure(const SND_PC_CONFIG* config);
/* Optional host-wide recursive lock, installed before sndInit. Both callbacks
 * must be supplied, or both NULL to use the adapter's default. They must remain
 * valid until replaced after sndQuit. This allows an application's I/O and
 * stream callbacks to share the engine's critical section without lock-order
 * inversion. The setting persists across sndQuit. */
bool sndPCSetSynchronization(void (*enter)(void), void (*leave)(void));
/* Fills exactly frames * channels samples. No allocation, device access, or
 * wall-clock dependence. Calls must be serialized with all other snd* calls.
 * The core retains one 5 ms cycle: control/lifetime queries may lead delivered
 * PCM by up to that cycle. A zero-frame request does not advance the engine.
 * Returns false without touching output if uninitialized or output is NULL. */
bool sndPCRender(s16* output, size_t frames);
SND_PC_RENDER_INFO sndPCGetRenderInfo(void);

/* Optional application mixer, called once per completed 5 ms cycle with the
 * interleaved output before it is queued. Runs under the render lock; it must
 * not allocate, block or reenter sndPCRender. Passing NULL detaches it.
 * Registration persists through sndInit and is cleared by sndQuit. */
typedef void (*SND_PC_OUTPUT_CALLBACK)(s16* output, size_t frames, u32 rate, u32 channels);
void sndPCSetOutputCallback(SND_PC_OUTPUT_CALLBACK callback);

typedef struct SND_PC_SPAN {
  const void* data;
  size_t size;
} SND_PC_SPAN;

typedef struct SND_PC_GROUP_ASSETS {
  SND_PC_SPAN project, pool, directory, samples;
} SND_PC_GROUP_ASSETS;

typedef struct SND_PC_ASSET_ERROR {
  const char* section;
  size_t offset;
  const char* reason;
} SND_PC_ASSET_ERROR;

/* Bounded validation of raw GC sections. Immutable, alignment-independent and
 * usable without sndInit or an output device. This checks file structure;
 * references to groups already on the stack are resolved when pushing. */
bool sndPCValidateGroup(const SND_PC_GROUP_ASSETS* assets, SND_PC_ASSET_ERROR* error);
bool sndPCValidateArrangement(SND_PC_SPAN input, SND_PC_ASSET_ERROR* error);

/* Optional SDL adapter, built with MUSYX_ENABLE_SDL. It uses the same renderer,
 * performs device-format conversion, and serializes calls via hwDisableIrq /
 * hwEnableIrq (as the original public APIs do). Start after sndInit; stop before
 * teardown. Pause preserves voices, the control clock, and queued audio. */
/* Open paused before sndInit. NULL/zero fields select the opened device's
 * actual rate/layout; automatic rates are clamped to 8..96 kHz, and unsupported
 * device layouts use stereo. Explicit values follow sndPCConfigure's limits.
 * The selected configuration is returned through obtained when non-NULL.
 * sndPCStopAudio closes a prepared device even if sndInit has not been called. */
bool sndPCOpenAudio(const SND_PC_CONFIG* preferred, SND_PC_CONFIG* obtained);
bool sndPCStartAudio(void);
void sndPCStopAudio(void);
void sndPCPauseAudio(bool paused);

#ifdef __cplusplus
}
#endif
#endif
