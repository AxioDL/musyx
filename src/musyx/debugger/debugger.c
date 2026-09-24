#include "musyx/debugger.h"
#include "musyx/synthdata.h"

#if MUSY_TARGET == MUSY_TARGET_PC

#include <stdatomic.h>
#include <string.h>

static _Atomic(bool) g_paused = false;
static _Atomic(bool) g_step_frame = false;
static _Atomic(u64) g_frame_count = 0;
static _Atomic(u64) g_macro_steps = 0;
static MUSYX_DEBUG_HOOKS g_hooks;
static _Atomic(u64) g_event_sequence = 0;
static _Atomic(u32) g_bp_macro[MUSYX_DEBUG_MAX_BREAKPOINTS];
static _Atomic(u32) g_bp_opcode[MUSYX_DEBUG_MAX_BREAKPOINTS];
static _Atomic(u32) g_bp_voice[MUSYX_DEBUG_MAX_BREAKPOINTS];
static _Atomic(bool) g_bp_enabled[MUSYX_DEBUG_MAX_BREAKPOINTS];

void musyxDebuggerSetHooks(const MUSYX_DEBUG_HOOKS* hooks) {
  if (hooks) {
    g_hooks = *hooks;
  } else {
    memset(&g_hooks, 0, sizeof(g_hooks));
  }
}

void musyxDebuggerSetPaused(bool8 paused) {
  atomic_store_explicit(&g_paused, paused != 0, memory_order_release);
}

bool8 musyxDebuggerIsPaused(void) {
  return atomic_load_explicit(&g_paused, memory_order_acquire) ? TRUE : FALSE;
}

void musyxDebuggerStepFrame(void) {
  atomic_store_explicit(&g_step_frame, true, memory_order_release);
  atomic_store_explicit(&g_paused, false, memory_order_release);
}

bool8 musyxDebuggerConsumeStepFrame(void) {
  return atomic_exchange_explicit(&g_step_frame, false, memory_order_acq_rel) ? TRUE : FALSE;
}

void musyxDebuggerSetBreakpoint(u32 index, const MUSYX_DEBUG_BREAKPOINT* breakpoint) {
  if (index >= MUSYX_DEBUG_MAX_BREAKPOINTS) return;
  if (!breakpoint) {
    atomic_store_explicit(&g_bp_enabled[index], false, memory_order_release);
    return;
  }
  atomic_store_explicit(&g_bp_macro[index], breakpoint->macroId, memory_order_relaxed);
  atomic_store_explicit(&g_bp_opcode[index], breakpoint->opcode, memory_order_relaxed);
  atomic_store_explicit(&g_bp_voice[index], breakpoint->voiceIndex, memory_order_relaxed);
  atomic_store_explicit(&g_bp_enabled[index], breakpoint->enabled != 0, memory_order_release);
}

void musyxDebuggerClearBreakpoints(void) {
  for (u32 i = 0; i < MUSYX_DEBUG_MAX_BREAKPOINTS; ++i)
    atomic_store_explicit(&g_bp_enabled[i], false, memory_order_release);
}

u64 musyxDebuggerGetFrameCount(void) {
  return atomic_load_explicit(&g_frame_count, memory_order_relaxed);
}

u64 musyxDebuggerGetMacroStepCount(void) {
  return atomic_load_explicit(&g_macro_steps, memory_order_relaxed);
}

u32 musyxDebuggerGetVoiceMacroCommands(const SYNTH_VOICE* voice,
                                        MUSYX_DEBUG_MACRO_COMMAND* out,
                                        u32 capacity) {
  if (!voice || !out || capacity == 0) return 0;

  /* voice->addr is the macro currently selected by the voice. Using it avoids
   * calling dataGetMacro() from the UI thread (that lookup uses legacy static
   * scratch state in the runtime). */
  const MSTEP* macro = voice->addr;
  if (!macro) return 0;

  /* Protect the UI from malformed/corrupt macro data. Real MusyX macros are
   * normally much shorter; this is only a debugger inspection bound. */
  const u32 maxCommands = capacity < 4096u ? capacity : 4096u;
  u32 count = 0;
  for (; count < maxCommands; ++count) {
    const MSTEP* step = &macro[count];
    MUSYX_DEBUG_MACRO_COMMAND* dst = &out[count];
    dst->index = count;
    dst->address = (uintptr_t)step;
    dst->opcode = (u16)(step->para[0] & 0x7fu);
    dst->reserved = 0;
    dst->arg0 = step->para[0];
    dst->arg1 = step->para[1];
    dst->current = (voice->curAddr != NULL && voice->curAddr > macro &&
                    voice->curAddr - 1 == step) ? TRUE : FALSE;

    /* Opcode zero is the END/STOP command and is part of the displayed list. */
    if (dst->opcode == 0) {
      ++count;
      break;
    }
  }
  return count;
}

static void emit(const MUSYX_DEBUG_EVENT* in) {
  if (!g_hooks.onEvent) return;
  MUSYX_DEBUG_EVENT e = *in;
  e.sequence = atomic_fetch_add_explicit(&g_event_sequence, 1, memory_order_relaxed);
  g_hooks.onEvent(&e, g_hooks.user);
}

void musyxDebuggerRuntimeFrame(u32 deltaTime) {
  (void)deltaTime;
  atomic_fetch_add_explicit(&g_frame_count, 1, memory_order_relaxed);
  MUSYX_DEBUG_EVENT e = {0};
  e.kind = MUSYX_DEBUG_EVENT_FRAME;
  e.realTime = synthRealTime;
  emit(&e);
}

/* Internal hooks used by the runtime. They remain C symbols so the core stays C-only. */
void musyxDebuggerRuntimeMacroStep(SYNTH_VOICE* voice, const MSTEP* step) {
  atomic_fetch_add_explicit(&g_macro_steps, 1, memory_order_relaxed);
  if (!voice || !step) return;
  MUSYX_DEBUG_EVENT e = {0};
  e.kind = MUSYX_DEBUG_EVENT_MACRO_STEP;
  e.realTime = synthRealTime;
  e.voiceIndex = voice->id & 0xff;
  e.voiceId = voice->id;
  e.macroId = voice->macroId;
  e.pc = (u32)(uintptr_t)voice->curAddr;
  e.opcode = (u16)(step->para[0] & 0x7f);
  e.arg0 = step->para[0];
  e.arg1 = step->para[1];

  for (u32 i = 0; i < MUSYX_DEBUG_MAX_BREAKPOINTS; ++i) {
    if (!atomic_load_explicit(&g_bp_enabled[i], memory_order_acquire)) continue;
    const u32 macro = atomic_load_explicit(&g_bp_macro[i], memory_order_relaxed);
    const u32 opcode = atomic_load_explicit(&g_bp_opcode[i], memory_order_relaxed);
    const u32 voiceIndex = atomic_load_explicit(&g_bp_voice[i], memory_order_relaxed);
    if (macro != 0xFFFFFFFFu && macro != e.macroId) continue;
    if (opcode != 0xFFFFFFFFu && opcode != e.opcode) continue;
    if (voiceIndex != 0xFFFFFFFFu && voiceIndex != e.voiceIndex) continue;
    atomic_store_explicit(&g_paused, true, memory_order_release);
    break;
  }
  emit(&e);
}

void musyxDebuggerRuntimeVoiceStart(SYNTH_VOICE* voice) {
  if (!voice) return;
  MUSYX_DEBUG_EVENT e = {0};
  e.kind = MUSYX_DEBUG_EVENT_VOICE_START;
  e.realTime = synthRealTime;
  e.voiceIndex = voice->id & 0xff;
  e.voiceId = voice->id;
  e.macroId = voice->macroId;
  e.pc = (u32)(uintptr_t)voice->curAddr;
  emit(&e);
}

void musyxDebuggerRuntimeVoiceStop(SYNTH_VOICE* voice) {
  if (!voice) return;
  MUSYX_DEBUG_EVENT e = {0};
  e.kind = MUSYX_DEBUG_EVENT_VOICE_STOP;
  e.realTime = synthRealTime;
  e.voiceIndex = voice->id & 0xff;
  e.voiceId = voice->id;
  e.macroId = voice->macroId;
  emit(&e);
}

void musyxDebuggerRuntimeMessage(u32 voiceId, s32 message) {
  MUSYX_DEBUG_EVENT e = {0};
  e.kind = MUSYX_DEBUG_EVENT_MESSAGE;
  e.realTime = synthRealTime;
  e.voiceId = voiceId;
  e.message = message;
  emit(&e);
}

void musyxDebuggerCapture(MUSYX_DEBUG_SNAPSHOT* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->frame = musyxDebuggerGetFrameCount();
  out->realTime = synthRealTime;
  out->synthInfo = synthInfo;
  out->voiceCount = synthInfo.voiceNum;
  if (out->voiceCount > SYNTH_MAX_VOICES) out->voiceCount = SYNTH_MAX_VOICES;

  if (synthVoice) {
    for (u32 i = 0; i < out->voiceCount; ++i) {
      out->voices[i] = synthVoice[i];
      if (synthVoice[i].id != SND_ID_ERROR && synthVoice[i].addr != NULL)
        ++out->activeVoiceCount;
    }
  }

  memcpy(out->sequences, seqInstance, sizeof(out->sequences));

  u32 streamCount = 0;
  const STREAM_INFO* streams = musyxDebuggerGetStreams(&streamCount);
  if (streams) {
    if (streamCount > 64) streamCount = 64;
    memcpy(out->streams, streams, sizeof(STREAM_INFO) * streamCount);
  }
}

#endif /* MUSY_TARGET == MUSY_TARGET_PC */
