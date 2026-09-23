#include "hw_pc_internal.h"
#include "musyx/hardware.h"

#include <SDL3/SDL.h>

static SDL_Mutex *mutex;
static SDL_AudioStream *stream;
static SDL_Thread *thread;
static SDL_AtomicInt running;
static SDL_AtomicInt paused;

static int audioThread(void *unused) {
  (void)unused;
  s16 buffer[SAL_PC_MAX_FRAMES * SAL_PC_MAX_CHANNELS];
  SND_PC_RENDER_INFO info = sndPCGetRenderInfo();
  size_t frames = (info.mixRate + 199) / 200;
  int bytes = (int)(frames * info.channels * sizeof(s16));
  SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
  while (SDL_GetAtomicInt(&running)) {
    /* At most 40 ms queued, in the stream's input format. */
    int queued = SDL_GetAudioStreamQueued(stream);
    if (queued < 0)
      break;
    if (SDL_GetAtomicInt(&paused) || queued >= bytes * 8) {
      SDL_Delay(1);
      continue;
    }
    hwIRQEnterCritical();
    bool ok = true;
    if (!SDL_GetAtomicInt(&paused) && SDL_GetAtomicInt(&running))
      ok = sndPCRender(buffer, frames) && SDL_PutAudioStreamData(stream, buffer, bytes);
    hwIRQLeaveCritical();
    if (!ok)
      break;
  }
  SDL_SetAtomicInt(&running, 0);
  return 0;
}

bool sndPCOpenAudio(const SND_PC_CONFIG *preferred, SND_PC_CONFIG *obtained) {
  SND_PC_CONFIG selected = preferred ? *preferred : (SND_PC_CONFIG){0, 0};
  if (sndIsInstalled() || stream || thread ||
      (selected.mixRate &&
       (selected.mixRate < SAL_PC_MIN_RATE || selected.mixRate > SAL_PC_MAX_RATE)) ||
      (selected.channels && selected.channels != 2 && selected.channels != 4 &&
       selected.channels != 6 && selected.channels != 8))
    return false;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    return false;
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL, NULL, NULL);
  if (!stream) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }
  SDL_AudioSpec device;
  if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, NULL)) {
    sndPCStopAudio();
    return false;
  }
  if (!selected.mixRate)
    selected.mixRate = CLAMP(device.freq, SAL_PC_MIN_RATE, SAL_PC_MAX_RATE);
  if (!selected.channels)
    selected.channels =
        device.channels == 4 || device.channels == 6 || device.channels == 8 ? device.channels : 2;
  SDL_AudioSpec input = {SDL_AUDIO_S16, (int)selected.channels, (int)selected.mixRate};
  if (!SDL_SetAudioStreamFormat(stream, &input, NULL) || !sndPCConfigure(&selected)) {
    sndPCStopAudio();
    return false;
  }
  if (obtained)
    *obtained = selected;
  return true;
}

bool sndPCStartAudio(void) {
  if (!sndIsInstalled() || thread)
    return false;
  SND_PC_RENDER_INFO info = sndPCGetRenderInfo();
  SDL_AudioSpec input = {SDL_AUDIO_S16, (int)info.channels, (int)info.mixRate};
  if (!stream) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
      return false;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &input, NULL, NULL);
    if (!stream) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      return false;
    }
  } else if (!SDL_SetAudioStreamFormat(stream, &input, NULL)) {
    sndPCStopAudio();
    return false;
  }
  SDL_SetAtomicInt(&paused, 0);
  SDL_SetAtomicInt(&running, 1);
  thread = SDL_CreateThread(audioThread, "MusyX Audio", NULL);
  if (!thread || !SDL_ResumeAudioStreamDevice(stream)) {
    sndPCStopAudio();
    return false;
  }
  return true;
}

void sndPCStopAudio(void) {
  SDL_SetAtomicInt(&running, 0);
  if (thread) {
    SDL_WaitThread(thread, NULL);
    thread = NULL;
  }
  if (stream) {
    SDL_DestroyAudioStream(stream);
    stream = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

void sndPCPauseAudio(bool value) {
  SDL_SetAtomicInt(&paused, value);
  /* Barrier with a batch already being rendered when pause was requested. */
  hwIRQEnterCritical();
  hwIRQLeaveCritical();
  if (stream) {
    if (value)
      SDL_PauseAudioStreamDevice(stream);
    else
      SDL_ResumeAudioStreamDevice(stream);
  }
}

void hwInitIrq(void) {
  mutex = SDL_CreateMutex();
  hwDisableIrq();
}
void hwExitIrq(void) {
  SDL_DestroyMutex(mutex);
  mutex = NULL;
}
void hwEnableIrq(void) {
  if (!salPCExternalLeave() && mutex)
    SDL_UnlockMutex(mutex);
}
void hwDisableIrq(void) {
  if (!salPCExternalEnter() && mutex)
    SDL_LockMutex(mutex);
}
void hwIRQEnterCritical(void) { hwDisableIrq(); }
void hwIRQLeaveCritical(void) { hwEnableIrq(); }
