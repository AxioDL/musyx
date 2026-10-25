#ifndef _MUSYX_VOICE_H_
#define _MUSYX_VOICE_H_

#include "musyx/synth.h"
#include "musyx/version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SYNTH_VOICELIST {
  // total size: 0x4
  u8 prev;  // offset 0x0, size 0x1
  u8 next;  // offset 0x1, size 0x1
  u16 user; // offset 0x2, size 0x2
} SYNTH_VOICELIST;
extern SYNTH_VOICELIST voicePrioSortVoices[64];
extern u8 voicePrioSortVoicesRoot[256];

typedef struct SYNTH_ROOTLIST {
  // total size: 0x4
  u16 next; // offset 0x0, size 0x2
  u16 prev; // offset 0x2, size 0x2
} SYNTH_ROOTLIST;

extern SYNTH_ROOTLIST voicePrioSortRootList[256];

extern u16 voicePrioSortRootListRoot;
extern u8 voiceMusicRunning;
extern u8 voiceFxRunning;
extern u8 voiceListInsert;
extern u8 voiceListRoot;

void voiceSetPriority(SYNTH_VOICE* svoice, u8 prio);
u32 voiceIsLastStarted(SYNTH_VOICE* svoice);
void voiceSetLastStarted(SYNTH_VOICE* svoice);
void voiceResetLastStarted(SYNTH_VOICE* svoice);
void voiceInitLastStarted();
s32 voiceKillSound(u32 voiceid);
void voiceKill(u32 vi);
void voiceSetPriority(SYNTH_VOICE* svoice, u8 prio);
u32 voiceIsLastStarted(SYNTH_VOICE* svoice);
void voiceSetLastStarted(SYNTH_VOICE* svoice);
void voiceResetLastStarted(SYNTH_VOICE* svoice);
void voiceInitLastStarted();
s32 voiceKillSound(u32 voiceid);
void voiceKill(u32 vi);
u32 voiceBlock(u8 prio);
u32 voiceAllocate(u8 priority, u8 maxVoices,
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
                  u32 allocId,
#else
                  u16 allocId,
#endif
                  u8 fxFlag);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
int voiceAllocatePeek(u8 priority, u8 maxVoices, u32 allocId, u8 fxFlag, u32* currentAllocId);
#endif

void voiceFree(SYNTH_VOICE* svoice);
void voiceUnblock(u32 voice);
void voiceRemovePriority(SYNTH_VOICE* svoice);

void synthKillVoicesByMacroReferences(u16* ref);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
void synthKillVoicesBySampleReferences(u16* ref);
#endif
void synthInitAllocationAids();
void synthKillAllVoices(bool8 musiconly);

void vidInit();
u32 vidGetInternalId(SND_VOICEID id);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
u32 vidGetPublicId(u32 voiceID);
#endif
void vidRemoveVoiceReferences(SYNTH_VOICE* svoice);
u32 vidMakeNew(SYNTH_VOICE* svoice, u32 isMaster);
u32 vidMakeRoot(SYNTH_VOICE* svoice);

#ifdef __cplusplus
}
#endif
#endif
