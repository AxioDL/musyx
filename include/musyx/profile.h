#ifndef _MUSYX_PROFILE
#define _MUSYX_PROFILE

#include "musyx/musyx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SND_PROFILE_DATA {
  // total size: 0x68
  unsigned long loadStores;       // offset 0x0, size 0x4
  unsigned long missCycles;       // offset 0x4, size 0x4
  unsigned long cycles;           // offset 0x8, size 0x4
  unsigned long instructions;     // offset 0xC, size 0x4
  unsigned long lastLoadStores;   // offset 0x10, size 0x4
  unsigned long lastMissCycles;   // offset 0x14, size 0x4
  unsigned long lastCycles;       // offset 0x18, size 0x4
  unsigned long lastInstructions; // offset 0x1C, size 0x4
  unsigned long peekLoadStores;   // offset 0x20, size 0x4
  unsigned long peekMissCycles;   // offset 0x24, size 0x4
  unsigned long peekCycles;       // offset 0x28, size 0x4
  unsigned long peekInstructions; // offset 0x2C, size 0x4
  unsigned long _loadStores;      // offset 0x30, size 0x4
  unsigned long _missCycles;      // offset 0x34, size 0x4
  unsigned long _cycles;          // offset 0x38, size 0x4
  unsigned long _instructions;    // offset 0x3C, size 0x4
  float avgLoadStores;            // offset 0x40, size 0x4
  float avgMissCycles;            // offset 0x44, size 0x4
  float avgCycles;                // offset 0x48, size 0x4
  float avgInstructions;          // offset 0x4C, size 0x4
  float sumLoadStores;            // offset 0x50, size 0x4
  float sumMissCycles;            // offset 0x54, size 0x4
  float sumCycles;                // offset 0x58, size 0x4
  float sumInstructions;          // offset 0x5C, size 0x4
  unsigned long cnt;              // offset 0x60, size 0x4
  unsigned long paused;           // offset 0x64, size 0x4
} SND_PROFILE_DATA;

typedef struct SND_PROFILE_INFO {
  // total size: 0x274
  SND_PROFILE_DATA dspCtrl;       // offset 0x0, size 0x68
  SND_PROFILE_DATA auxProcessing; // offset 0x68, size 0x68
  SND_PROFILE_DATA sequencer;     // offset 0xD0, size 0x68
  SND_PROFILE_DATA synthesizer;   // offset 0x138, size 0x68
  SND_PROFILE_DATA emitters;      // offset 0x1A0, size 0x68
  SND_PROFILE_DATA streaming;     // offset 0x208, size 0x68
  unsigned char numMusicVoices;   // offset 0x270, size 0x1
  unsigned char numSFXVoices;     // offset 0x271, size 0x1
} SND_PROFILE_INFO;
typedef void (*SND_PROF_USERCALLBACK)(struct SND_PROFILE_INFO *); // size: 0x4, address: 0x0

#ifdef MUSY_BUILD_DEBUG
extern SND_PROF_USERCALLBACK sndProfUserCallback;

#if MUSY_VERSION < MUSY_VERSION_CHECK(1, 5, 1)
void sndProfSetCallback(SND_PROF_USERCALLBACK callback);
void sndProfUpdateMisc(SND_PROFILE_INFO* info);
void sndProfResetPMC(SND_PROFILE_DATA* info);
void sndProfStartPMC(SND_PROFILE_DATA* info);
void sndProfPausePMC(SND_PROFILE_DATA* info);
void sndProfStopPMC(SND_PROFILE_DATA* info);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // _MUSYX_PROFILE
