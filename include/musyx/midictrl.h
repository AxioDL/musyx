#ifndef _MUSYX_MIDICTRL
#define _MUSYX_MIDICTRL

#include "musyx/musyx.h"
#include "musyx/synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct CHANNEL_DEFAULTS {
  // total size: 0x9
  u8 pbRange; // offset 0x0, size 0x1
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
  u32 lpfLowerFrqBoundary; // offset 0x1, size 0x4
  u32 lpfUpperFrqBoundary; // offset 0x5, size 0x4
#endif
} CHANNEL_DEFAULTS;
#pragma pack(pop)

u8 inpTranslateExCtrl(u8 ctrl);
void inpSetGlobalMIDIDirtyFlag(u8 chan, u8 midiSet, s32 flag);
void inpAddCtrl(CTRL_DEST* dest, u8 ctrl, s32 scale, u8 comb, u32 isVar);
void inpSetMidiCtrl(u8 ctrl, u8 channel, u8 set, u8 value);
void inpSetMidiCtrl14(u8 ctrl, u8 channel, u8 set, u16 value);
void inpSetExCtrl(SYNTH_VOICE* svoice, u8 ctrl, s16 v);
CHANNEL_DEFAULTS* inpGetChannelDefaults(u8 midi, u8 midiSet);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
unsigned short inpGetFilterSwitch(struct SYNTH_VOICE* svoice);
unsigned short inpGetFilterParameter(struct SYNTH_VOICE* svoice);
void inpSetLPFDefaultRange(u32 lowFrq, u32 highFrq);
#endif
void inpSetMidiLastNote(u8 midi, u8 midiSet, u8 key);
u8 inpGetMidiLastNote(u8 midi, u8 midiSet);
u16 inpGetExCtrl(SYNTH_VOICE* svoice, u8 ctrl);
u16 inpGetMidiCtrl(u8 ctrl, u8 channel, u8 set);
#if MUSY_VERSION < MUSY_VERSION_CHECK(1, 5, 2)
// Removed in 1.5 Patch 2 (note 50)
void inpSetMidiPortState(u8 midi, u8 midiSet, u8 state);
u8 inpGetMidiPortState(u8 midi, u8 midiSet);
u16 inpGetPortamento(SYNTH_VOICE* svoice);
#endif
u16 inpGetModulation(SYNTH_VOICE* svoice);
void inpResetMidiCtrl(u8 ch, u8 set, u32 coldReset);
void inpResetChannelDefaults(u8 midi, u8 midiSet);
u16 inpGetPitchBend(SYNTH_VOICE* svoice);
u16 inpGetDoppler(SYNTH_VOICE* svoice);
u16 inpGetTremolo(SYNTH_VOICE* svoice);
u16 inpGetPanning(SYNTH_VOICE* svoice);
u16 inpGetSurPanning(SYNTH_VOICE* svoice);
u16 inpGetVolume(SYNTH_VOICE* svoice);
u16 inpGetReverb(SYNTH_VOICE* svoice);
u16 inpGetPreAuxA(SYNTH_VOICE* svoice);
u16 inpGetPostAuxA(SYNTH_VOICE* svoice);
u16 inpGetPreAuxB(SYNTH_VOICE* svoice);
u16 inpGetPostAuxB(SYNTH_VOICE* svoice);
u16 inpGetPedal(SYNTH_VOICE* svoice);
u16 inpGetAuxA(u8 studio, u8 index, u8 midi, u8 midiSet);
u16 inpGetAuxB(u8 studio, u8 index, u8 midi, u8 midiSet);
void inpInit(SYNTH_VOICE* svoice);
void inpFXCopyCtrl(u8 ctrl, SYNTH_VOICE* dvoice, SYNTH_VOICE* svoice);

#ifdef __cplusplus
}
#endif

#endif // _MUSYX_MIDICTRL
