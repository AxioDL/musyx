#ifndef DSPVOICE_H
#define DSPVOICE_H

#include "musyx/musyx.h"

#include "musyx/synthdata.h"
#include "musyx/voice.h"

#define DSP_PB_STATE_STOPPED 0
#define DSP_PB_STATE_PLAYING 1

#define DSP_VOICE_STATE_INACTIVE 0
#define DSP_VOICE_STATE_STARTING 1
#define DSP_VOICE_STATE_PLAYING 2

#define DSP_STUDIO_STATE_INACTIVE 0
#define DSP_STUDIO_STATE_ACTIVE 1

typedef struct _PBMIX {
  // total size: 0x24
  u16 vL;          // offset 0x0, size 0x2
  u16 vDeltaL;     // offset 0x2, size 0x2
  u16 vR;          // offset 0x4, size 0x2
  u16 vDeltaR;     // offset 0x6, size 0x2
  u16 vAuxAL;      // offset 0x8, size 0x2
  u16 vDeltaAuxAL; // offset 0xA, size 0x2
  u16 vAuxAR;      // offset 0xC, size 0x2
  u16 vDeltaAuxAR; // offset 0xE, size 0x2
  u16 vAuxBL;      // offset 0x10, size 0x2
  u16 vDeltaAuxBL; // offset 0x12, size 0x2
  u16 vAuxBR;      // offset 0x14, size 0x2
  u16 vDeltaAuxBR; // offset 0x16, size 0x2
  u16 vAuxBS;      // offset 0x18, size 0x2
  u16 vDeltaAuxBS; // offset 0x1A, size 0x2
  u16 vS;          // offset 0x1C, size 0x2
  u16 vDeltaS;     // offset 0x1E, size 0x2
  u16 vAuxAS;      // offset 0x20, size 0x2
  u16 vDeltaAuxAS; // offset 0x22, size 0x2
} _PBMIX;

typedef struct _PBITD {
  // total size: 0xE
  u16 flag;         // offset 0x0, size 0x2
  u16 bufferHi;     // offset 0x2, size 0x2
  u16 bufferLo;     // offset 0x4, size 0x2
  u16 shiftL;       // offset 0x6, size 0x2
  u16 shiftR;       // offset 0x8, size 0x2
  u16 targetShiftL; // offset 0xA, size 0x2
  u16 targetShiftR; // offset 0xC, size 0x2
} _PBITD;

typedef struct _PBUPDATE {
  // total size: 0xE
  u16 updNum[5]; // offset 0x0, size 0xA
  u16 dataHi;    // offset 0xA, size 0x2
  u16 dataLo;    // offset 0xC, size 0x2
} _PBUPDATE;

typedef struct _PBDPOP {
  // total size: 0x12
  u16 aL;     // offset 0x0, size 0x2
  u16 aAuxAL; // offset 0x2, size 0x2
  u16 aAuxBL; // offset 0x4, size 0x2
  u16 aR;     // offset 0x6, size 0x2
  u16 aAuxAR; // offset 0x8, size 0x2
  u16 aAuxBR; // offset 0xA, size 0x2
  u16 aS;     // offset 0xC, size 0x2
  u16 aAuxAS; // offset 0xE, size 0x2
  u16 aAuxBS; // offset 0x10, size 0x2
} _PBDPOP;

typedef struct _PBVE {
  // total size: 0x4
  u16 currentVolume; // offset 0x0, size 0x2
  u16 currentDelta;  // offset 0x2, size 0x2
} _PBVE;

typedef struct _PBFIR {
  // total size: 0x6
  u16 numCoefs; // offset 0x0, size 0x2
  u16 coefsHi;  // offset 0x2, size 0x2
  u16 coefsLo;  // offset 0x4, size 0x2
} _PBFIR;

typedef struct _PBADDR {
  // total size: 0x10
  u16 loopFlag;         // offset 0x0, size 0x2
  u16 format;           // offset 0x2, size 0x2
  u16 loopAddressHi;    // offset 0x4, size 0x2
  u16 loopAddressLo;    // offset 0x6, size 0x2
  u16 endAddressHi;     // offset 0x8, size 0x2
  u16 endAddressLo;     // offset 0xA, size 0x2
  u16 currentAddressHi; // offset 0xC, size 0x2
  u16 currentAddressLo; // offset 0xE, size 0x2
} _PBADDR;

typedef struct _PBADPCM {
  // total size: 0x28
  u16 a[8][2];    // offset 0x0, size 0x20
  u16 gain;       // offset 0x20, size 0x2
  u16 pred_scale; // offset 0x22, size 0x2
  u16 yn1;        // offset 0x24, size 0x2
  u16 yn2;        // offset 0x26, size 0x2
} _PBADPCM;

typedef struct _PBSRC {
  // total size: 0xE
  u16 ratioHi;            // offset 0x0, size 0x2
  u16 ratioLo;            // offset 0x2, size 0x2
  u16 currentAddressFrac; // offset 0x4, size 0x2
  u16 last_samples[4];    // offset 0x6, size 0x8
} _PBSRC;

typedef struct _PBADPCMLOOP {
  // total size: 0x6
  u16 loop_pred_scale; // offset 0x0, size 0x2
  u16 loop_yn1;        // offset 0x2, size 0x2
  u16 loop_yn2;        // offset 0x4, size 0x2
} _PBADPCMLOOP;

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
typedef struct _PBLPF {
  // total size: 0x8
  unsigned short flag; // offset 0x0, size 0x2
  unsigned short yn1;  // offset 0x2, size 0x2
  unsigned short a0;   // offset 0x4, size 0x2
  unsigned short b0;   // offset 0x6, size 0x2
} _PBLPF;
#endif

typedef struct _PB {
  // total size: 0xBC
  u16 nextHi;             // offset 0x0, size 0x2
  u16 nextLo;             // offset 0x2, size 0x2
  u16 currHi;             // offset 0x4, size 0x2
  u16 currLo;             // offset 0x6, size 0x2
  u16 srcSelect;          // offset 0x8, size 0x2
  u16 coefSelect;         // offset 0xA, size 0x2
  u16 mixerCtrl;          // offset 0xC, size 0x2
  u16 state;              // offset 0xE, size 0x2
  u16 loopType;           // offset 0x10, size 0x2
  _PBMIX mix;             // offset 0x12, size 0x24
  _PBITD itd;             // offset 0x36, size 0xE
  _PBUPDATE update;       // offset 0x44, size 0xE
  _PBDPOP dpop;           // offset 0x52, size 0x12
  _PBVE ve;               // offset 0x64, size 0x4
  _PBFIR fir;             // offset 0x68, size 0x6
  _PBADDR addr;           // offset 0x6E, size 0x10
  _PBADPCM adpcm;         // offset 0x7E, size 0x28
  _PBSRC src;             // offset 0xA6, size 0xE
  _PBADPCMLOOP adpcmLoop; // offset 0xB4, size 0x6
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
  _PBLPF lpf; // offset 0xBA, size 0x8
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
  u16 streamLoopCnt; // offset 0xBA, size 0x2
#endif
} _PB;

typedef struct VSampleInfo {
  // total size: 0xC
  void* loopBufferAddr; // offset 0x0, size 0x4
  u32 loopBufferLength; // offset 0x4, size 0x4
  u8 inLoopBuffer;      // offset 0x8, size 0x1
} VSampleInfo;

typedef struct _SPB {
  // total size: 0x36
  u16 dpopLHi;     // offset 0x0, size 0x2
  u16 dpopLLo;     // offset 0x2, size 0x2
  u16 dpopLDelta;  // offset 0x4, size 0x2
  u16 dpopRHi;     // offset 0x6, size 0x2
  u16 dpopRLo;     // offset 0x8, size 0x2
  u16 dpopRDelta;  // offset 0xA, size 0x2
  u16 dpopSHi;     // offset 0xC, size 0x2
  u16 dpopSLo;     // offset 0xE, size 0x2
  u16 dpopSDelta;  // offset 0x10, size 0x2
  u16 dpopALHi;    // offset 0x12, size 0x2
  u16 dpopALLo;    // offset 0x14, size 0x2
  u16 dpopALDelta; // offset 0x16, size 0x2
  u16 dpopARHi;    // offset 0x18, size 0x2
  u16 dpopARLo;    // offset 0x1A, size 0x2
  u16 dpopARDelta; // offset 0x1C, size 0x2
  u16 dpopASHi;    // offset 0x1E, size 0x2
  u16 dpopASLo;    // offset 0x20, size 0x2
  u16 dpopASDelta; // offset 0x22, size 0x2
  u16 dpopBLHi;    // offset 0x24, size 0x2
  u16 dpopBLLo;    // offset 0x26, size 0x2
  u16 dpopBLDelta; // offset 0x28, size 0x2
  u16 dpopBRHi;    // offset 0x2A, size 0x2
  u16 dpopBRLo;    // offset 0x2C, size 0x2
  u16 dpopBRDelta; // offset 0x2E, size 0x2
  u16 dpopBSHi;    // offset 0x30, size 0x2
  u16 dpopBSLo;    // offset 0x32, size 0x2
  u16 dpopBSDelta; // offset 0x34, size 0x2
} _SPB;

typedef struct FILTERInfo {
  // total size: 0x6
  u8 on;     // offset 0x0, size 0x1
  u16 coefA; // offset 0x2, size 0x2
  u16 coefB; // offset 0x4, size 0x2
} FILTERInfo;

typedef struct DSPvoice {
  _PB* pb;
  void* patchData;
  void* itdBuffer;
  struct DSPvoice* next;
  struct DSPvoice* prev;
  struct DSPvoice* nextAlien;
  u32 mesgCallBackUserValue;
  u32 prio;
  u32 currentAddr;
  u32 changed[5];
  u32 pitch[5];
  u16 volL;
  u16 volR;
  u16 volS;
  u16 volLa;
  u16 volRa;
  u16 volSa;
  u16 volLb;
  u16 volRb;
  u16 volSb;
  u16 lastVolL;
  u16 lastVolR;
  u16 lastVolS;
  u16 lastVolLa;
  u16 lastVolRa;
  u16 lastVolSa;
  u16 lastVolLb;
  u16 lastVolRb;
  u16 lastVolSb;
  u16 smp_id;
  SAMPLE_INFO smp_info;
  VSampleInfo vSampleInfo;
  u8 streamLoopPS;
  ADSR_VARS adsr;
  u16 srcTypeSelect;
  u16 srcCoefSelect;
  u16 itdShiftL;
  u16 itdShiftR;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
  FILTERInfo filter;
#endif
  u8 singleOffset;
  struct {
    u32 posHi;
    u32 posLo;
    u32 pitch;
  } playInfo;

  struct {
    u8 pitch;
    u8 vol;
    u8 volA;
    u8 volB;
  } lastUpdate;

#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
  u32 virtualSampleID;
#endif
  u8 state;
  u8 postBreak;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
  u8 startupBreak;
#endif
  u8 studio;
  u32 flags;
#if MUSY_TARGET == MUSY_TARGET_PC
  u32 pan, span;
  u16 volRear[2], lastVolRear[2];
#endif
} DSPvoice;

typedef struct DSPhostDPop {
  // total size: 0x24
  s32 l;  // offset 0x0, size 0x4
  s32 r;  // offset 0x4, size 0x4
  s32 s;  // offset 0x8, size 0x4
  s32 lA; // offset 0xC, size 0x4
  s32 rA; // offset 0x10, size 0x4
  s32 sA; // offset 0x14, size 0x4
  s32 lB; // offset 0x18, size 0x4
  s32 rB; // offset 0x1C, size 0x4
  s32 sB; // offset 0x20, size 0x4
} DSPhostDPop;

typedef struct DSPinput {
  // total size: 0xC
  u8 studio;              // offset 0x0, size 0x1
  u16 vol;                // offset 0x2, size 0x2
  u16 volA;               // offset 0x4, size 0x2
  u16 volB;               // offset 0x6, size 0x2
  SND_STUDIO_INPUT* desc; // offset 0x8, size 0x4
} DSPinput;

typedef struct DSPstudioinfo {
  // total size: 0xBC
  _SPB* spb;                    // offset 0x0, size 0x4
  DSPhostDPop hostDPopSum;      // offset 0x4, size 0x24
  s32* main[2];                 // offset 0x28, size 0x8
  s32* auxA[3];                 // offset 0x30, size 0xC
  s32* auxB[3];                 // offset 0x3C, size 0xC
  DSPvoice* voiceRoot;          // offset 0x48, size 0x4
  DSPvoice* alienVoiceRoot;     // offset 0x4C, size 0x4
  u8 state;                     // offset 0x50, size 0x1
  u8 isMaster;                  // offset 0x51, size 0x1
  u8 numInputs;                 // offset 0x52, size 0x1
  SND_STUDIO_TYPE type;         // offset 0x54, size 0x4
  DSPinput in[7];               // offset 0x58, size 0x54
  SND_AUX_CALLBACK auxAHandler; // offset 0xAC, size 0x4
  SND_AUX_CALLBACK auxBHandler; // offset 0xB0, size 0x4
  void* auxAUser;               // offset 0xB4, size 0x4
  void* auxBUser;               // offset 0xB8, size 0x4
} DSPstudioinfo;

extern DSPstudioinfo dspStudio[8];
extern DSPvoice* dspVoice;
extern bool dspCompressorOn;
extern u32 dspHRTFOn;
extern u16* dspCmdList;
extern u16 dspCmdFirstSize;
#endif
