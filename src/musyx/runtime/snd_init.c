
/*















*/

#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/vsamples.h"
#include "musyx/aramdma.h"
#include "musyx/s3d.h"
#include "musyx/seq.h"
#include "musyx/midictrl.h"
#include "musyx/stream.h"
#include "musyx/synth.h"
#include "musyx/synthdata.h"
#if MUSY_TARGET == MUSY_TARGET_PC
#include "hw_pc_assets.h"
#include "hw_pc_internal.h"
#include "musyx/dspvoice.h"
#include "musyx/pc.h"
#endif

// #define _DEBUG

/*




*/
// TODO: Platform specific audio memory handling
#if MUSY_VERSION < MUSY_VERSION_CHECK(1, 5, 2)
static s32 DoInit(u32 mixFrq, u32 aramSize, u32 numVoices)
#elif MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 0)
static s32 DoInit(u32 mixFrq, u32 aramSize, u32 numVoices, u32 flags)
#else
long DoInit(u32 mixFrq, u32 numVoices, u32 flags, u32 aramBase, u32 aramSize)
#endif
{
  bool ret;

#ifdef MUSY_BUILD_DEBUG
  // Only in debug builds: the empty release MUSY_DEBUG would leave the __DATE__/__TIME__
  // expression behind and change sndInit's stack layout once DoInit is inlined
  MUSY_DEBUG("\nMusyX software initialization...\nBuild Date: %s %s\n\n", __DATE__, __TIME__);
#endif
  ret = FALSE;

#if MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 0)
  dataInitStack();
#elif MUSY_VERSION == MUSY_VERSION_CHECK(2, 0, 1)
  dataInitStack(aramBase, aramSize);
#else
  dataInitStack(aramBase, aramGetUserBytes(aramSize));
#endif
  dataInit(0, aramSize);

  seqInit();

  synthIdleWaitActive = 0;

  synthInit(mixFrq, numVoices);
#if MUSY_TARGET == MUSY_TARGET_PC
  if (!synthVoice) {
    dataExit();
    return -1;
  }
#endif

  streamInit();

#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
  vsInit();

#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
  s3dInit(flags);
#else
  s3dInit();
#endif

  sndActive = 1;

  MUSY_DEBUG("MusyX logic is initialized.\n\n");

  return ret;
}

/*










*/
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 1)
s32 sndInit(u8 voices, u8 music, u8 sfx, u8 studios, u32 flags, u32 aramSize) {
#else
s32 sndInit(u8 voices, u8 music, u8 sfx, u32 flags, u32 aramSize) {
#endif
  s32 ret; // r31
  u32 frq; // r1+0x14

#if MUSY_TARGET == MUSY_TARGET_PC
  if (sndActive || voices == 0 || studios == 0)
    return -1;
#endif

  MUSY_DEBUG("Entering sndInit()\n\n");
  ret = 0;
  sndActive = 0;
  if (voices <= 64) {
    synthInfo.voiceNum = voices;
  } else {
    synthInfo.voiceNum = 64;
  }
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 1)
  if (studios <= 8) {
    synthInfo.studioNum = studios;
  } else {
    synthInfo.studioNum = 8;
  }
#endif

  synthInfo.maxMusic = music;
  synthInfo.maxSFX = sfx;
#if MUSY_TARGET == MUSY_TARGET_PC
  frq = 32000;
#define FRQ frq
#else
  frq = 32000;
#define FRQ 32000
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 1)
  if ((ret = hwInit(&frq, synthInfo.voiceNum, synthInfo.studioNum, flags)) == 0) {
#else
  if ((ret = hwInit(&frq, synthInfo.voiceNum, flags)) == 0) {
#endif
#if MUSY_VERSION < MUSY_VERSION_CHECK(1, 5, 2)
    ret = DoInit(FRQ, aramSize, synthInfo.voiceNum);
#elif MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 0)
    ret = DoInit(FRQ, aramSize, synthInfo.voiceNum, flags);
#else
    ret = DoInit(FRQ, synthInfo.voiceNum, flags, aramGetFirstUserAddress(), aramSize);
#endif
  }
#undef FRQ

#if MUSY_TARGET == MUSY_TARGET_PC
  if (ret != 0 && dspVoice)
    hwExit();
#endif

  MUSY_DEBUG("Leaving sndInit().\n\n");
  return ret;
}

/* */
void sndQuit() {
#if MUSY_TARGET == MUSY_TARGET_PC
  if (!sndActive)
    return;
  sndPCStopAudio();
  salPCExitStreams();
  sndVirtualSampleFreeBuffers();
  seqKillAllInstances();
  while (sndPopGroup()) {
  }
  salPCCollectSongs();
#endif
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");

  hwExit();
  /*


  */
  dataExit();
  /*
   */
  s3dExit();
  /* */
  synthExit();
  /* */
  sndActive = 0;
}

/*


*/
void sndSetMaxVoices(u8 music, u8 sfx) {
  MUSY_ASSERT_MSG(music <= synthInfo.voiceNum, "Music voices are above maximum voice number.");
  MUSY_ASSERT_MSG(sfx <= synthInfo.voiceNum, "Sfx voices are above maximum voice number.");

  synthInfo.maxMusic = music;
  synthInfo.maxSFX = sfx;
}

/*


*/
bool sndIsInstalled() { return sndActive; }

/*




*/
SND_PLAYBACKINFO *sndGetPlayBackInfo() {
  if (sndActive) {
    return &synthInfo.pbInfo;
  }

  return NULL;
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
void sndSetLowPassFilterDefaultRange(unsigned long lowFrq, unsigned long highFrq) {
  inpSetLPFDefaultRange(lowFrq, highFrq);
}
#endif
