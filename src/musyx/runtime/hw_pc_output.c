#include "hw_pc_internal.h"
#include "musyx/hardware.h"
#include "musyx/synth.h"

#include <string.h>

static SND_PC_CONFIG config = {32000, 2};
static SND_PC_RENDER_INFO info;
static SND_SOME_CALLBACK controlCallback;
static SND_PC_OUTPUT_CALLBACK outputCallback;
static s16 cycle[SAL_PC_MAX_FRAMES * SAL_PC_MAX_CHANNELS];
static size_t cycleRead, cycleFrames;
static void (*externalEnter)(void), (*externalLeave)(void);
extern u32 last_rnd;

void sndPCSetOutputCallback(SND_PC_OUTPUT_CALLBACK callback) {
  hwIRQEnterCritical();
  outputCallback = callback;
  hwIRQLeaveCritical();
}

bool sndPCSetSynchronization(void (*enter)(void), void (*leave)(void)) {
  if (sndActive || (!!enter != !!leave))
    return false;
  externalEnter = enter;
  externalLeave = leave;
  return true;
}
bool salPCExternalEnter(void) {
  if (!externalEnter)
    return false;
  externalEnter();
  return true;
}
bool salPCExternalLeave(void) {
  if (!externalLeave)
    return false;
  externalLeave();
  return true;
}

bool sndPCConfigure(const SND_PC_CONFIG *requested) {
  if (sndActive || !requested ||
      (requested->mixRate < SAL_PC_MIN_RATE || requested->mixRate > SAL_PC_MAX_RATE) ||
      (requested->channels != 2 && requested->channels != 4 && requested->channels != 6 &&
       requested->channels != 8))
    return false;
  config = *requested;
  return true;
}

u32 salPCMixRate(void) { return config.mixRate; }
u32 salPCChannels(void) { return config.channels; }

void salPCResetOutput(SND_SOME_CALLBACK callback) {
  last_rnd = 1;
  controlCallback = callback;
  memset(&info, 0, sizeof(info));
  info.mixRate = config.mixRate;
  info.channels = config.channels;
  cycleRead = cycleFrames = 0;
  memset(cycle, 0, sizeof(cycle));
}

void *salAiGetDest(void) { return cycle; }

bool sndPCRender(s16 *output, size_t frames) {
  if (!sndActive || (!output && frames) || frames > SIZE_MAX / (config.channels * sizeof(s16)))
    return false;
  hwIRQEnterCritical();
  while (frames) {
    if (cycleRead == cycleFrames) {
      const u64 end = salPCBoundary(info.controlTicks + 5, config.mixRate);
      cycleFrames = (size_t)(end - info.renderedFrames);
      cycleRead = 0;
      salPCBeginCycle(info.controlTicks);
      controlCallback();
      if (outputCallback)
        outputCallback(cycle, cycleFrames, config.mixRate, config.channels);
      info.renderedFrames = end;
      info.controlTicks += 5;
    }
    size_t count = cycleFrames - cycleRead;
    if (count > frames)
      count = frames;
    memcpy(output, cycle + cycleRead * config.channels, count * config.channels * sizeof(s16));
    output += count * config.channels;
    cycleRead += count;
    info.outputFrames += count;
    frames -= count;
  }
  hwIRQLeaveCritical();
  return true;
}

SND_PC_RENDER_INFO sndPCGetRenderInfo(void) {
  hwIRQEnterCritical();
  SND_PC_RENDER_INFO result = info;
  hwIRQLeaveCritical();
  return result;
}

bool salStartAi(void) { return true; }
bool salExitAi(void) {
  controlCallback = NULL;
  outputCallback = NULL;
  cycleRead = cycleFrames = 0;
  return true;
}
