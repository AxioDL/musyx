#ifndef MUSYX_DEBUGGER_H
#define MUSYX_DEBUGGER_H

#if MUSY_TARGET == MUSY_TARGET_PC

#include "musyx/musyx.h"
#include "musyx/seq.h"
#include "musyx/stream.h"
#include "musyx/synth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MUSYX_DEBUG_EVENT_KIND {
  MUSYX_DEBUG_EVENT_FRAME = 0,
  MUSYX_DEBUG_EVENT_MACRO_STEP,
  MUSYX_DEBUG_EVENT_VOICE_START,
  MUSYX_DEBUG_EVENT_VOICE_STOP,
  MUSYX_DEBUG_EVENT_MESSAGE,
} MUSYX_DEBUG_EVENT_KIND;

typedef struct MUSYX_DEBUG_EVENT {
  u64 sequence;
  u64 realTime;
  u32 kind;
  u32 voiceIndex;
  u32 voiceId;
  u16 macroId;
  u16 opcode;
  u32 pc;
  u32 arg0;
  u32 arg1;
  s32 message;
} MUSYX_DEBUG_EVENT;

typedef struct MUSYX_DEBUG_HOOKS {
  void (*onEvent)(const MUSYX_DEBUG_EVENT* event, void* user);
  void* user;
} MUSYX_DEBUG_HOOKS;

/* Install a lightweight runtime hook. The callback runs on the audio/synth thread. */
void musyxDebuggerSetHooks(const MUSYX_DEBUG_HOOKS* hooks);

/* Runtime execution controls. These are intentionally lock-free. */
void musyxDebuggerSetPaused(bool8 paused);
bool8 musyxDebuggerIsPaused(void);
void musyxDebuggerStepFrame(void);
bool8 musyxDebuggerConsumeStepFrame(void);

#define MUSYX_DEBUG_MAX_BREAKPOINTS 64
typedef struct MUSYX_DEBUG_BREAKPOINT {
  u32 macroId;    /* 0xFFFFFFFF = any */
  u32 opcode;     /* 0xFFFFFFFF = any */
  u32 voiceIndex; /* 0xFFFFFFFF = any */
  bool8 enabled;
} MUSYX_DEBUG_BREAKPOINT;

void musyxDebuggerSetBreakpoint(u32 index, const MUSYX_DEBUG_BREAKPOINT* breakpoint);
void musyxDebuggerClearBreakpoints(void);

/* Runtime instrumentation state. */
u64 musyxDebuggerGetFrameCount(void);
u64 musyxDebuggerGetMacroStepCount(void);

/* Exposes the stream table for inspection without making its storage public. */
const STREAM_INFO* musyxDebuggerGetStreams(u32* count);

/* Macro command inspection for the client-side debugger UI.
 * The returned count is the number of commands copied into out. The command
 * list is terminated by opcode 0 (end-of-macro) or by capacity.
 */
typedef struct MUSYX_DEBUG_MACRO_COMMAND {
  u32 index;
  uintptr_t address;
  u16 opcode;
  u16 reserved;
  u32 arg0;
  u32 arg1;
  bool8 current;
} MUSYX_DEBUG_MACRO_COMMAND;

u32 musyxDebuggerGetVoiceMacroCommands(const SYNTH_VOICE* voice,
                                        MUSYX_DEBUG_MACRO_COMMAND* out,
                                        u32 capacity);

/* Returns a stable snapshot of the current runtime. The returned data is owned by the caller. */
typedef struct MUSYX_DEBUG_SNAPSHOT {
  u64 frame;
  u64 realTime;
  SynthInfo synthInfo;
  u32 voiceCount;
  u32 activeVoiceCount;
  SYNTH_VOICE voices[SYNTH_MAX_VOICES];
  SEQ_INSTANCE sequences[SND_MAX_SEQINSTANCES];
  STREAM_INFO streams[64];
} MUSYX_DEBUG_SNAPSHOT;

void musyxDebuggerCapture(MUSYX_DEBUG_SNAPSHOT* out);

/* Internal runtime instrumentation entry points. */
void musyxDebuggerRuntimeFrame(u32 deltaTime);
void musyxDebuggerRuntimeMacroStep(SYNTH_VOICE* voice, const MSTEP* step);
void musyxDebuggerRuntimeVoiceStart(SYNTH_VOICE* voice);
void musyxDebuggerRuntimeVoiceStop(SYNTH_VOICE* voice);
void musyxDebuggerRuntimeMessage(u32 voiceId, s32 message);

#ifdef __cplusplus
}
#endif

#endif /* MUSY_TARGET == MUSY_TARGET_PC */

#endif /* MUSYX_DEBUGGER_H */
