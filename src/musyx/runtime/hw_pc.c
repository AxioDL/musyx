#include "musyx/platform.h"

#include "hw_pc_internal.h"
#include "hw_pc_decode.h"
#include <math.h>

#include <string.h>

#include "musyx/adsr.h"
#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/musyx.h"
#include "musyx/sal.h"
#include "musyx/synth.h"

// Audio parameters
#define SAL_SAMPLES_PER_FRAME SAL_PC_MAX_FRAMES
#define SAL_SUBFRAMES 5
static u32 cycleFrames;
static u32 segmentOffset[SAL_SUBFRAMES + 1];
static u64 cycleTick;
static s32 effectHistory[8][2][2][3][32];
static s32 studioHistory[8][7][5][2];
static s32 rearMain[8][2][2][SAL_PC_MAX_FRAMES];
static s32 legacyWork[3][160];
static s32 adaptedWork[5][SAL_PC_MAX_FRAMES];

/* Detached, bounded stop tails: no decoder/sample pointer survives hwOff, and
 * reusing a voice cannot truncate the preceding generation's ramp. */
#define DEPOP_RING 1024
static s32 depop[8][11][DEPOP_RING];
static s32 lastContribution[SYNTH_MAX_VOICES][11];
static u32 depopCursor, renderOffset;

void salPCBeginCycle(u64 tick) {
  cycleTick = tick;
  u32 rate = salPCMixRate();
  u64 begin = salPCBoundary(tick, rate);
  for (u32 i = 0; i <= 5; ++i)
    segmentOffset[i] = (u32)(salPCBoundary(tick + i, rate) - begin);
  cycleFrames = segmentOffset[5];
}

static void resetStudioHistory(u8 studio) {
  memset(effectHistory[studio], 0, sizeof(effectHistory[studio]));
  memset(studioHistory[studio], 0, sizeof(studioHistory[studio]));
}
#define POLYPHASE_PHASES 128
#define POLYPHASE_TAPS 32
#define POLYPHASE_BANDS 64
#define POLYPHASE_SCALE (1 << 23)
typedef struct VoiceResamplerState {
  MusyPCMReader reader;
  u32 phase;
  s16 history[POLYPHASE_TAPS];
  u32 historyIndex;
  u8 tail;
  s16 itdHistory[128];
  u32 itdIndex;
  u32 itdDelay[2];
} VoiceResamplerState;

static VoiceResamplerState voiceResampler[SYNTH_MAX_VOICES];
static s32 polyphaseTable[POLYPHASE_BANDS][POLYPHASE_PHASES][POLYPHASE_TAPS];
static u8 resampleTablesInitialized = 0;

// Mix accumulation buffers
static s32 mixBufferL[SAL_SAMPLES_PER_FRAME];
static s32 mixBufferR[SAL_SAMPLES_PER_FRAME];
static s32 mixBufferRear[2][SAL_SAMPLES_PER_FRAME];
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
static s32 filterState[SYNTH_MAX_VOICES];
#endif

#define SURROUND_DOWNMIX_GAIN 23170

static inline s16 clamp16(s32 v) {
  if (v > 32767)
    return 32767;
  if (v < -32768)
    return -32768;
  return (s16)v;
}

static double sincUnit(double x) {
  if (fabs(x) < 1e-12) return 1.0;
  const double pix = x * 3.14159265358979323846;
  return sin(pix) / pix;
}

static void initResampleTables(void) {
  if (resampleTablesInitialized) return;
  /* Generated Blackman-windowed sinc filters, independently authored for the
   * native renderer. Each phase has exact unity DC gain in Q23. Filter banks
   * lower the cutoff as source pitch rises, rather than aliasing downsampling. */
  for (u32 band = 0; band < POLYPHASE_BANDS; ++band) {
    double cutoff = (double)(band + 1) / POLYPHASE_BANDS;
    for (u32 phase = 0; phase < POLYPHASE_PHASES; ++phase) {
      double coefficients[POLYPHASE_TAPS], sum = 0;
      double center = POLYPHASE_TAPS / 2 - 1 + (double)phase / POLYPHASE_PHASES;
      for (u32 tap = 0; tap < POLYPHASE_TAPS; ++tap) {
        double angle = 2 * 3.14159265358979323846 * tap / (POLYPHASE_TAPS - 1);
        double window = 0.42 - 0.5 * cos(angle) + 0.08 * cos(2 * angle);
        coefficients[tap] = cutoff * sincUnit(cutoff * (tap - center)) * window;
        sum += coefficients[tap];
      }
      s32 total = 0;
      for (u32 tap = 0; tap < POLYPHASE_TAPS; ++tap) {
        s32 value = (s32)lround(coefficients[tap] / sum * POLYPHASE_SCALE);
        polyphaseTable[band][phase][tap] = value;
        total += value;
      }
      polyphaseTable[band][phase][POLYPHASE_TAPS / 2] += POLYPHASE_SCALE - total;
    }
  }
  resampleTablesInitialized = 1;
}

const s32* salPCRateCoefficients(u32 inputRate, u32 outputRate, u32 fraction) {
  /* Leave room for the finite filter's transition band below the destination
   * Nyquist frequency. This also limits phase-dependent alias energy. */
  u32 band = inputRate > outputRate ? (u64)outputRate * POLYPHASE_BANDS * 9 / (inputRate * 10) : POLYPHASE_BANDS;
  if (!band) band = 1;
  return polyphaseTable[band - 1][fraction >> 9];
}

static s32 mixRampChannel(s32* dest, const s32* src, int nSamples, s16 startVol,
                           s16 endVol, u16 envStart, u16 envEnd, int frameOffset) {
  if (!dest || (!startVol && !endVol))
    return 0;
  s32 contribution = 0;
  for (int i = 0; i < nSamples; ++i) {
    s32 env = envStart + ((s32)envEnd - envStart) * i / nSamples;
    s32 vol = startVol + ((s32)endVol - startVol) * (frameOffset + i) / (s32)cycleFrames;
    s32 mixVol = (vol * env) >> 15;
    contribution = (src[i] * mixVol) >> 15;
    dest[i] += contribution;
  }
  return contribution;
}

static void retireContribution(DSPvoice* voice) {
  s32* last = lastContribution[voice - dspVoice];
  const u32 frames = (salPCMixRate() + 199) / 200;
  for (u32 channel = 0; channel < 11; ++channel) {
    for (u32 frame = 0; frame < frames; ++frame)
      depop[voice->studio][channel][(depopCursor + renderOffset + frame) & (DEPOP_RING - 1)] +=
          (s32)((s64)last[channel] * (frames - frame - 1) / frames);
    last[channel] = 0;
  }
}

static void mixStudioOutput(const s32* left, const s32* right, const s32* surround) {
  for (int i = 0; i < cycleFrames; ++i) {
    mixBufferL[i] += left[i];
    mixBufferR[i] += right[i];
    if (surround != NULL) {
      s32 surroundMix = (s32)((s64)surround[i] * SURROUND_DOWNMIX_GAIN >> 15);
      if (salPCChannels() == 2) {
        mixBufferL[i] += surroundMix;
        mixBufferR[i] += surroundMix;
      } else {
        mixBufferRear[0][i] += surroundMix;
        mixBufferRear[1][i] += surroundMix;
      }
    }
  }
}

static void foldStereoToOutput(const s32* left, const s32* right) {
  for (int i = 0; i < cycleFrames; ++i) {
    mixBufferL[i] += left[i];
    mixBufferR[i] += right[i];
  }
}

/* Causal generated polyphase interpolation. Decoder history and sample position
 * advance only as the renderer consumes source samples; there is no speculative
 * decode across a stream refill boundary. */
static int resampleVoice(DSPvoice* voice, u32 voiceIdx, s32* out, int numSamples, u32 pitch) {
  VoiceResamplerState* state = &voiceResampler[voiceIdx];
  const u32 mode = voice->srcTypeSelect;
  if (mode == SAL_SRC_NONE) pitch = 65536; /* SDK SRC_NONE always runs at the mixing rate. */
  u32 filter = voice->srcCoefSelect;
  u32 cutoff = filter == 0 ? 32768 : filter == 1 ? 52428 : 65536;
  if (pitch > 65536) cutoff = (u32)(((u64)cutoff * 65536) / pitch);
  u32 band = cutoff * POLYPHASE_BANDS / 65536;
  if (!band) band = 1;
  if (band > POLYPHASE_BANDS) band = POLYPHASE_BANDS;
  for (int i = 0; i < numSamples; ++i) {
    state->phase += pitch;
    while (state->phase >= 65536) {
      /* A virtual sample first consumes its uploaded prefix, then continues
       * in the callback-owned ring without resetting ADPCM history. */
      SAMPLE_INFO* source = &voice->smp_info;
      if (source->compType == SAMPLE_TYPE_ADPCM_VIRTUAL && !voice->vSampleInfo.inLoopBuffer &&
          source->loopLength &&
          state->reader.position >= source->loop + source->loopLength) {
        source->addr = voice->vSampleInfo.loopBufferAddr;
        source->length = source->loopLength = voice->vSampleInfo.loopBufferLength;
        source->loop = 0;
        voice->vSampleInfo.inLoopBuffer = 1;
        state->reader.position = 0;
        state->reader.predictorScale = voice->streamLoopPS;
        state->reader.useInitialPS = true;
      }
      s16 sample = salPCReadSample(&state->reader, &voice->smp_info, voice->streamLoopPS);
      state->history[state->historyIndex++ & (POLYPHASE_TAPS - 1)] = sample;
      if (state->reader.ended && state->tail < POLYPHASE_TAPS)
        ++state->tail;
      state->phase -= 65536;
    }
    if (mode == SAL_SRC_NONE) {
      out[i] = state->history[(state->historyIndex - 1) & (POLYPHASE_TAPS - 1)];
    } else if (mode == SAL_SRC_LINEAR) {
      s32 older = state->history[(state->historyIndex - 2) & (POLYPHASE_TAPS - 1)];
      s32 newer = state->history[(state->historyIndex - 1) & (POLYPHASE_TAPS - 1)];
      out[i] = older + (s32)((s64)(newer - older) * state->phase / 65536);
    } else {
      const s32* coefficients = polyphaseTable[band - 1][state->phase >> 9];
      s64 result = 0;
      for (u32 tap = 0; tap < POLYPHASE_TAPS; ++tap)
        result += (s64)state->history[(state->historyIndex + tap) & (POLYPHASE_TAPS - 1)] * coefficients[tap];
      out[i] = clamp16((s32)(result >> 23));
    }
  }
  voice->playInfo.posHi = state->reader.position;
  voice->playInfo.posLo = state->phase;
  return numSamples;
}

/*
 * Software voice rendering for one frame (SAL_SAMPLES_PER_FRAME samples).
 * Decodes samples, then mixes into main and AUX buffers with per-channel volumes.
 */
static s32 voiceDecodeBuf[SAL_SAMPLES_PER_FRAME];
static s32 itdOutput[2][SAL_SAMPLES_PER_FRAME];

static u32 itdTarget(const DSPvoice* voice, u32 channel) {
  if (!(voice->flags & 0x80000000)) return 0;
  u32 shift = channel ? voice->itdShiftR : voice->itdShiftL;
  if (shift > 32) shift = 32;
  return (u32)(((u64)shift * salPCMixRate() * 65536) / 32000);
}

static void applyITD(const DSPvoice* voice, VoiceResamplerState* state, int frames, int offset) {
  u32 targets[2] = {itdTarget(voice, 0), itdTarget(voice, 1)};
  for (int i = 0; i < frames; ++i) {
    state->itdHistory[state->itdIndex & 127] = voiceDecodeBuf[i];
    for (u32 channel = 0; channel < 2; ++channel) {
      s64 delay = state->itdDelay[channel] +
                  ((s64)targets[channel] - state->itdDelay[channel]) * (offset + i) / cycleFrames;
      u32 index = state->itdIndex - (u32)(delay >> 16);
      s32 newer = state->itdHistory[index & 127];
      s32 older = state->itdHistory[(index - 1) & 127];
      itdOutput[channel][i] = newer + (s32)((s64)(older - newer) * (delay & 0xffff) / 65536);
    }
    ++state->itdIndex;
  }
}


// Returns 1 if voice finished playing (end of non-looping sample), 0 otherwise.
static int renderVoiceSegment(DSPvoice* vp, s32* mainL, s32* mainR, s32* mainS, s32* auxAL,
                              s32* auxAR, s32* auxAS, s32* auxBL, s32* auxBR, s32* auxBS,
                              u32 voiceIdx, int frameOffset, int frameSamples, u16 adsrStart,
                              u16 adsrEnd) {
  if (vp->state == DSP_VOICE_STATE_INACTIVE)
    return 0;

  const SAMPLE_INFO* smp = &vp->smp_info;
  if (smp->addr == NULL)
    return 0;

  u32 pitch = vp->playInfo.pitch;
  if (pitch == 0)
    pitch = vp->pitch[0];
  if (pitch == 0 && vp->srcTypeSelect != SAL_SRC_NONE)
    return 0;

  /* Decode samples into temp buffer */
  VoiceResamplerState* state = &voiceResampler[voiceIdx];
  int nSamples = resampleVoice(vp, voiceIdx, voiceDecodeBuf, frameSamples, pitch);
  u32 tail = vp->srcTypeSelect == SAL_SRC_NONE ? 1 : vp->srcTypeSelect == SAL_SRC_LINEAR ? 2 : POLYPHASE_TAPS;
  int voiceDone = state->tail >= tail;

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
  if (vp->filter.on) {
    s32 yn = filterState[voiceIdx];
    for (int i = 0; i < nSamples; ++i) {
      yn = ((s32)vp->filter.coefA * voiceDecodeBuf[i] + (s32)vp->filter.coefB * yn) >> 15;
      voiceDecodeBuf[i] = yn;
    }
    filterState[voiceIdx] = yn;
  }
#endif

  applyITD(vp, state, nSamples, frameOffset);
  lastContribution[voiceIdx][0] = mixRampChannel(mainL ? mainL + frameOffset : NULL, itdOutput[0], nSamples,
                 (s16)vp->lastVolL, (s16)vp->volL, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][1] = mixRampChannel(mainR ? mainR + frameOffset : NULL, itdOutput[1], nSamples,
                 (s16)vp->lastVolR, (s16)vp->volR, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][2] = mixRampChannel(mainS ? mainS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolS, (s16)vp->volS, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][3] = mixRampChannel(auxAL ? auxAL + frameOffset : NULL, itdOutput[0], nSamples,
                 (s16)vp->lastVolLa, (s16)vp->volLa, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][4] = mixRampChannel(auxAR ? auxAR + frameOffset : NULL, itdOutput[1], nSamples,
                 (s16)vp->lastVolRa, (s16)vp->volRa, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][5] = mixRampChannel(auxAS ? auxAS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolSa, (s16)vp->volSa, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][6] = mixRampChannel(auxBL ? auxBL + frameOffset : NULL, itdOutput[0], nSamples,
                 (s16)vp->lastVolLb, (s16)vp->volLb, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][7] = mixRampChannel(auxBR ? auxBR + frameOffset : NULL, itdOutput[1], nSamples,
                 (s16)vp->lastVolRb, (s16)vp->volRb, adsrStart, adsrEnd, frameOffset);
  lastContribution[voiceIdx][8] = mixRampChannel(auxBS ? auxBS + frameOffset : NULL, voiceDecodeBuf, nSamples,
                 (s16)vp->lastVolSb, (s16)vp->volSb, adsrStart, adsrEnd, frameOffset);

  for (u32 channel = 0; channel < 2; ++channel)
    lastContribution[voiceIdx][9 + channel] = mixRampChannel(
        rearMain[vp->studio][salFrame][channel] + frameOffset, itdOutput[channel], nSamples,
        vp->lastVolRear[channel], vp->volRear[channel], adsrStart, adsrEnd, frameOffset);
  return voiceDone;
}

void salCtrlDsp(s16* dest) {
  u8 st;
  DSPstudioinfo* stp;

  renderOffset = 0;
  memset(mixBufferL, 0, sizeof(mixBufferL));
  memset(mixBufferR, 0, sizeof(mixBufferR));
  memset(mixBufferRear, 0, sizeof(mixBufferRear));

  for (st = 0, stp = dspStudio; st < salMaxStudioNum; ++st, ++stp) {
    if (stp->state != DSP_STUDIO_STATE_ACTIVE)
      continue;

    memset(rearMain[st][salFrame], 0, sizeof(rearMain[st][salFrame]));

    /* Clear the current frame buffers only. Previous-frame data feeds studio inputs. */
    if (stp->main[salFrame])
      memset(stp->main[salFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));

    /* Clear AUX buffers for the current aux frame */
    if (stp->auxA[salAuxFrame])
      memset(stp->auxA[salAuxFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));
    if (stp->auxB[salAuxFrame])
      memset(stp->auxB[salAuxFrame], 0, SAL_SAMPLES_PER_FRAME * 3 * sizeof(s32));

    /* Render all voices in this studio */
    DSPvoice* vp = stp->voiceRoot;
    while (vp != NULL) {
      renderOffset = 0;
      DSPvoice* nextVp = vp->next; /* save in case voice is deactivated */
      if (vp->state != DSP_VOICE_STATE_INACTIVE) {
        u32 voiceIdx = (u32)(vp - dspVoice);
        u8 mixStart = 0;

        /* Breaks end the previous generation. Reusing an active voice carries
         * the break bit into a new startup, which must not release the new note. */
        if (vp->postBreak || (vp->changed[0] & 0x20)) {
          if (vp->state != DSP_VOICE_STATE_STARTING || vp->startupBreak) {
            salDeactivateVoice(vp);
            vp->startupBreak = 0;
            vp = nextVp;
            continue;
          }
          vp->changed[0] &= ~0x20u;
        }

        /* New voice initialization (mirrors salBuildCommandList startup path) */
        if (vp->state == DSP_VOICE_STATE_STARTING) {
          if (adsrSetup(&vp->adsr) != 0) {
            salSynthSendMessage(vp, HW_MESSAGE_SAMPLE_END);
            salDeactivateVoice(vp);
            vp = nextVp;
            continue;
          }

          vp->virtualSampleID = UINT32_MAX;
          if (vp->smp_info.compType == SAMPLE_TYPE_ADPCM_VIRTUAL) {
            vp->vSampleInfo.loopBufferAddr = NULL;
            vp->vSampleInfo.loopBufferLength = 0;
            vp->vSampleInfo.inLoopBuffer = 0;
            vp->virtualSampleID = salSynthSendMessage(vp, HW_MESSAGE_VIRTUAL_SAMPLE_START);
            if (!vp->vSampleInfo.loopBufferAddr || !vp->vSampleInfo.loopBufferLength) {
              salSynthSendMessage(vp, HW_MESSAGE_VOICE_KILL);
              salDeactivateVoice(vp);
              vp = nextVp;
              continue;
            }
          }

          vp->lastVolL = vp->volL;
          vp->lastVolR = vp->volR;
          vp->lastVolS = vp->volS;
          vp->lastVolLa = vp->volLa;
          vp->lastVolRa = vp->volRa;
          vp->lastVolSa = vp->volSa;
          vp->lastVolLb = vp->volLb;
          vp->lastVolRb = vp->volRb;
          vp->lastVolSb = vp->volSb;
          vp->lastVolRear[0] = vp->volRear[0];
          vp->lastVolRear[1] = vp->volRear[1];

          VoiceResamplerState* native = &voiceResampler[voiceIdx];
          memset(native, 0, sizeof(*native));
          native->itdDelay[0] = itdTarget(vp, 0);
          native->itdDelay[1] = itdTarget(vp, 1);
          if (!salPCInitReader(&native->reader, &vp->smp_info)) {
            salSynthSendMessage(vp, HW_MESSAGE_SAMPLE_END);
            salDeactivateVoice(vp);
            vp = nextVp;
            continue;
          }
          vp->playInfo.posHi = native->reader.position;
          vp->playInfo.posLo = 0;
          vp->playInfo.pitch = 0;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
          filterState[voiceIdx] = 0;
#endif
          mixStart = vp->singleOffset;
          vp->state = DSP_VOICE_STATE_PLAYING;
          renderOffset = segmentOffset[mixStart];
        }

        /* Get studio main buffer pointers */
        s32* studioMain = stp->main[salFrame];
        s32* studioL = studioMain;
        s32* studioR = studioMain ? studioMain + SAL_SAMPLES_PER_FRAME : NULL;
        s32* studioS = studioMain ? studioMain + SAL_SAMPLES_PER_FRAME * 2 : NULL;

        /* Get AUX buffer pointers */
        s32* auxACur = stp->auxA[salAuxFrame];
        s32* auxAL = auxACur;
        s32* auxAR = auxACur ? auxACur + SAL_SAMPLES_PER_FRAME : NULL;
        s32* auxAS = auxACur ? auxACur + SAL_SAMPLES_PER_FRAME * 2 : NULL;
        s32* auxBCur = stp->auxB[salAuxFrame];
        s32* auxBL = auxBCur;
        s32* auxBR = auxBCur ? auxBCur + SAL_SAMPLES_PER_FRAME : NULL;
        s32* auxBS = auxBCur ? auxBCur + SAL_SAMPLES_PER_FRAME * 2 : NULL;

        if (studioMain != NULL) {
          int finished = 0;

          for (u8 subframe = mixStart; subframe < SAL_SUBFRAMES; ++subframe) {
            renderOffset = segmentOffset[subframe];
            if ((vp->changed[subframe] & 0x20) != 0) {
              vp->postBreak = 1;
              salDeactivateVoice(vp);
              finished = 1;
              break;
            } else if (vp->postBreak == 0) {
              if ((vp->changed[subframe] & 0x40) != 0) {
                adsrRelease(&vp->adsr);
              }
              if ((vp->changed[subframe] & 8) != 0)
                vp->playInfo.pitch = vp->pitch[subframe];
            }

            if ((vp->changed[subframe] & 0x10) != 0) {
              if (adsrSetup(&vp->adsr) != 0) {
                salSynthSendMessage(vp, HW_MESSAGE_SAMPLE_END);
                salDeactivateVoice(vp);
                finished = 1;
                break;
              }
            }

            u16 adsrStart = 0;
            u16 adsrDelta = 0;
            u32 adsrDone = adsrHandle(&vp->adsr, &adsrStart, &adsrDelta);
            int sampleOffset = segmentOffset[subframe];
            int sampleCount = segmentOffset[subframe + 1] - sampleOffset;
            u16 adsrEnd = CLAMP(vp->adsr.currentVolume >> 16, 0, 0x7fff);

            int sampleDone = renderVoiceSegment(vp, studioL, studioR, studioS, auxAL, auxAR, auxAS, auxBL, auxBR,
                                                 auxBS, voiceIdx, sampleOffset, sampleCount, adsrStart, adsrEnd);
            renderOffset = segmentOffset[subframe + 1];
            if (sampleDone) {
              salSynthSendMessage(vp, HW_MESSAGE_SAMPLE_END);
              salDeactivateVoice(vp);
              finished = 1;
              break;
            }

            if (adsrDone) {
              salSynthSendMessage(vp, HW_MESSAGE_SAMPLE_END);
              salDeactivateVoice(vp);
              finished = 1;
              break;
            }
          }

          vp->lastVolL = vp->volL;
          vp->lastVolR = vp->volR;
          vp->lastVolS = vp->volS;
          vp->lastVolLa = vp->volLa;
          vp->lastVolRa = vp->volRa;
          vp->lastVolSa = vp->volSa;
          vp->lastVolLb = vp->volLb;
          vp->lastVolRb = vp->volRb;
          vp->lastVolSb = vp->volSb;
          vp->lastVolRear[0] = vp->volRear[0];
          vp->lastVolRear[1] = vp->volRear[1];

          voiceResampler[voiceIdx].itdDelay[0] = itdTarget(vp, 0);
          voiceResampler[voiceIdx].itdDelay[1] = itdTarget(vp, 1);

          if (finished) {
            vp = nextVp;
            continue;
          }
        }
      }
      vp = nextVp;
    }

    for (u32 channel = 0; channel < 11; ++channel) {
      s32* buffer = channel < 3 ? stp->main[salFrame] : channel < 6 ? stp->auxA[salAuxFrame] : stp->auxB[salAuxFrame];
      if (channel >= 9) buffer = rearMain[st][salFrame][channel - 9];
      else buffer += (channel % 3) * SAL_PC_MAX_FRAMES;
      for (u32 frame = 0; frame < cycleFrames; ++frame) {
        u32 index = (depopCursor + frame) & (DEPOP_RING - 1);
        buffer[frame] += depop[st][channel][index];
        depop[st][channel][index] = 0;
      }
    }

    if (stp->main[salFrame] != NULL) {
      s32* studioMain = stp->main[salFrame];
      s32* auxACur = stp->auxA[salAuxFrame];
      s32* auxBCur = stp->auxB[salAuxFrame];
      for (u8 inputIdx = 0; inputIdx < stp->numInputs; ++inputIdx) {
        DSPinput* input = &stp->in[inputIdx];
        DSPstudioinfo* srcStudio = &dspStudio[input->studio];
        s32* srcMain = srcStudio->main[salFrame ^ 1];
        if (srcMain == NULL)
          continue;

        u32 rate = salPCMixRate();
        u64 previousTick = cycleTick >= 5 ? cycleTick - 5 : 0;
        u32 previousFrames = cycleTick ? (u32)(salPCBoundary(cycleTick, rate) -
                                               salPCBoundary(previousTick, rate)) : cycleFrames;
        for (u32 channel = 0; channel < 5; ++channel) {
          const s32* source = channel < 3 ? srcMain + channel * SAL_SAMPLES_PER_FRAME :
                                                    rearMain[input->studio][salFrame ^ 1][channel - 3];
          if (previousFrames == cycleFrames && rate % 200 == 0) {
            memcpy(adaptedWork[channel], source, cycleFrames * sizeof(s32));
          } else {
            salPCConvertStudioCycle(adaptedWork[channel], cycleFrames, rate, cycleTick,
                              source, previousFrames, rate, previousTick,
                              studioHistory[st][inputIdx][channel]);
          }
          u32 base = channel * SAL_SAMPLES_PER_FRAME;
          for (u32 frame = 0; frame < cycleFrames; ++frame) {
            s32 value = adaptedWork[channel][frame];
            if (channel < 3) {
              studioMain[base + frame] += (s32)((s64)value * input->vol >> 15);
            } else {
              rearMain[st][salFrame][channel - 3][frame] += (s32)((s64)value * input->vol >> 15);
              value = (s32)((s64)value * SURROUND_DOWNMIX_GAIN >> 15);
              base = 2 * SAL_PC_MAX_FRAMES;
            }
            auxACur[base + frame] += (s32)((s64)value * input->volA >> 15);
            auxBCur[base + frame] += (s32)((s64)value * input->volB >> 15);
          }
        }
      }
    }

    /* Legacy callbacks always receive three 160-sample, 32 kHz buffers. The
     * previous cycle is processed, preserving the existing triple rotation. */
    for (u32 bus = 0; bus < 2; ++bus) {
      SND_AUX_CALLBACK callback = bus ? stp->auxBHandler : stp->auxAHandler;
      if (!callback || (bus && stp->type == SND_STUDIO_TYPE_DPL2))
        continue;
      s32* work = (bus ? stp->auxB : stp->auxA)[(salAuxFrame + 2) % 3];
      u32 rate = salPCMixRate();
      u64 previousTick = cycleTick >= 5 ? cycleTick - 5 : 0;
      u32 previousFrames = cycleTick ? (u32)(salPCBoundary(cycleTick, rate) -
                                             salPCBoundary(previousTick, rate)) : cycleFrames;
      for (u32 channel = 0; channel < 3; ++channel) {
        const s32* source = work + channel * SAL_SAMPLES_PER_FRAME;
        if (rate == 32000)
          memcpy(legacyWork[channel], source, sizeof(legacyWork[channel]));
        else
          salPCConvertCycle(legacyWork[channel], 160, 32000, previousTick,
                            source, previousFrames, rate, previousTick,
                            effectHistory[st][bus][0][channel]);
      }
      SND_AUX_INFO info;
      info.data.bufferUpdate.left = legacyWork[0];
      info.data.bufferUpdate.right = legacyWork[1];
      info.data.bufferUpdate.surround = legacyWork[2];
      callback(SND_AUX_REASON_BUFFERUPDATE, &info, bus ? stp->auxBUser : stp->auxAUser);
      for (u32 channel = 0; channel < 3; ++channel) {
        if (rate == 32000)
          memcpy(adaptedWork[channel], legacyWork[channel], sizeof(legacyWork[channel]));
        else
          salPCConvertCycle(adaptedWork[channel], cycleFrames, rate, cycleTick,
                            legacyWork[channel], 160, 32000, cycleTick,
                            effectHistory[st][bus][1][channel]);
        s32* main = stp->main[salFrame] + channel * SAL_SAMPLES_PER_FRAME;
        for (u32 frame = 0; frame < cycleFrames; ++frame)
          main[frame] += adaptedWork[channel][frame];
      }
    }

    /* Accumulate master studio into final mix */
    if (stp->isMaster) {
      s32* studioMain = stp->main[salFrame];
      if (studioMain != NULL) {
        s32* studioL = studioMain;
        s32* studioR = studioMain + SAL_SAMPLES_PER_FRAME;
        s32* studioS = studioMain + SAL_SAMPLES_PER_FRAME * 2;
        mixStudioOutput(studioL, studioR, stp->type == SND_STUDIO_TYPE_DPL2 && salPCChannels() == 2 ? NULL : studioS);
        for (u32 channel = 0; channel < 2; ++channel)
          for (u32 frame = 0; frame < cycleFrames; ++frame)
            mixBufferRear[channel][frame] += rearMain[st][salFrame][channel][frame];
      }

      if (stp->type == SND_STUDIO_TYPE_DPL2 && salPCChannels() == 2) {
        s32* dpl2Rear = stp->auxB[salAuxFrame];
        if (dpl2Rear != NULL)
          foldStereoToOutput(dpl2Rear, dpl2Rear + SAL_SAMPLES_PER_FRAME);
      }
    }
  }

  depopCursor = (depopCursor + cycleFrames) & (DEPOP_RING - 1);
  renderOffset = 0;

  if (dest) {
    u32 channels = salPCChannels();
    for (u32 i = 0; i < cycleFrames; ++i) {
      s16* frame = dest + i * channels;
      frame[0] = clamp16(mixBufferL[i]);
      frame[1] = clamp16(mixBufferR[i]);
      if (channels > 2) {
        u32 rear = channels == 4 ? 2 : 4;
        s32 left = mixBufferRear[0][i], right = mixBufferRear[1][i];
        if (channels >= 6) frame[2] = frame[3] = 0;
        if (channels == 8) {
          left = (s32)((s64)left * SURROUND_DOWNMIX_GAIN >> 15);
          right = (s32)((s64)right * SURROUND_DOWNMIX_GAIN >> 15);
          frame[6] = clamp16(left);
          frame[7] = clamp16(right);
        }
        frame[rear] = clamp16(left);
        frame[rear + 1] = clamp16(right);
      }
    }
  }
}

bool salInitAi(SND_SOME_CALLBACK callback, u32 flags, u32* outFreq) {
  (void)flags;
  salPCResetOutput(callback);
  memset(voiceResampler, 0, sizeof(voiceResampler));
  memset(lastContribution, 0, sizeof(lastContribution));
  memset(depop, 0, sizeof(depop));
  depopCursor = renderOffset = 0;
  initResampleTables();
  synthInfo.numSamples = 32;
  *outFreq = salPCMixRate();
  return true;
}

bool salInitDsp(u32 flags) {
  (void)flags;
  return true;
}
bool salExitDsp(void) { return true; }
void salStartDsp(u16* cmdList) { (void)cmdList; }

bool salInitDspCtrl(u8 voices, u8 studios, u32 dpl2) {
  salNumVoices = voices;
  salMaxStudioNum = studios;
  memset(dspStudio, 0, sizeof(dspStudio));
  dspVoice = salMalloc(voices * sizeof(*dspVoice));
  if (!dspVoice)
    return false;
  memset(dspVoice, 0, voices * sizeof(*dspVoice));
  for (u32 i = 0; i < voices; ++i) {
    dspVoice[i].virtualSampleID = 0xffffffff;
    memset(&dspVoice[i].lastUpdate, 0xff, sizeof(dspVoice[i].lastUpdate));
  }
  for (u32 i = 0; i < studios; ++i) {
    DSPstudioinfo* studio = &dspStudio[i];
    studio->main[0] = salMalloc(SAL_PC_STUDIO_BYTES);
    if (!studio->main[0]) {
      salExitDspCtrl();
      return false;
    }
    memset(studio->main[0], 0, SAL_PC_STUDIO_BYTES);
    studio->main[1] = studio->main[0] + 3 * SAL_SAMPLES_PER_FRAME;
    for (u32 j = 0; j < 3; ++j) {
      studio->auxA[j] = studio->main[0] + (2 + j) * 3 * SAL_SAMPLES_PER_FRAME;
      studio->auxB[j] = studio->main[0] + (5 + j) * 3 * SAL_SAMPLES_PER_FRAME;
    }
  }
  salActivateStudio(0, 1, dpl2 ? SND_STUDIO_TYPE_DPL2 : SND_STUDIO_TYPE_STD);
  return true;
}

bool salExitDspCtrl(void) {
  for (u32 i = 0; i < salMaxStudioNum; ++i) {
    if (dspStudio[i].main[0])
      salFree(dspStudio[i].main[0]);
  }
  memset(dspStudio, 0, sizeof(dspStudio));
  if (dspVoice)
    salFree(dspVoice);
  dspVoice = NULL;
  return true;
}

/* The native renderer owns studio storage and rate-adapter history. Keep the
 * shared sal* boundary, with each backend defining its own implementation. */
void salActivateStudio(u8 studio, u32 isMaster, SND_STUDIO_TYPE type) {
  DSPstudioinfo* state = &dspStudio[studio];
  memset(state->main[0], 0, SAL_PC_STUDIO_BYTES);
  resetStudioHistory(studio);
  memset(rearMain[studio], 0, sizeof(rearMain[studio]));
  memset(depop[studio], 0, sizeof(depop[studio]));
  state->voiceRoot = NULL;
  state->alienVoiceRoot = NULL;
  state->state = DSP_STUDIO_STATE_ACTIVE;
  state->isMaster = isMaster;
  state->numInputs = 0;
  state->type = type;
  state->auxAHandler = state->auxBHandler = NULL;
  state->auxAUser = state->auxBUser = NULL;
}

/* Hardware HRTF storage is not used by the native panner. */
void salInitHRTFBuffer(void) {}

/* Native teardown also retires virtual instances, including voice stealing and
 * explicit hwOff. Unlink first so a STOP callback can safely reenter APIs. */
void salDeactivateVoice(DSPvoice* voice) {
  if (voice->state == DSP_VOICE_STATE_INACTIVE) return;
  retireContribution(voice);
  if (voice->prev) voice->prev->next = voice->next;
  else dspStudio[voice->studio].voiceRoot = voice->next;
  if (voice->next) voice->next->prev = voice->prev;
  voice->state = DSP_VOICE_STATE_INACTIVE;
  if (voice->virtualSampleID != UINT32_MAX) {
    salSynthSendMessage(voice, HW_MESSAGE_VIRTUAL_SAMPLE_END);
    voice->virtualSampleID = UINT32_MAX;
  }
  voice->vSampleInfo.inLoopBuffer = 0;
}

void salReconnectVoice(DSPvoice* voice, u8 studio) {
  if (voice->studio == studio) return;
  if (voice->state != DSP_VOICE_STATE_INACTIVE) {
    if (voice->state == DSP_VOICE_STATE_PLAYING) {
      /* The old studio owns its detached tail. The live decoder, envelope and
       * virtual instance continue in the new studio with a 5 ms gain ramp. */
      retireContribution(voice);
      voice->lastVolL = voice->lastVolR = voice->lastVolS = 0;
      voice->lastVolLa = voice->lastVolRa = voice->lastVolSa = 0;
      voice->lastVolLb = voice->lastVolRb = voice->lastVolSb = 0;
      voice->lastVolRear[0] = voice->lastVolRear[1] = 0;
    }
    if (voice->prev) voice->prev->next = voice->next;
    else dspStudio[voice->studio].voiceRoot = voice->next;
    if (voice->next) voice->next->prev = voice->prev;
    voice->next = dspStudio[studio].voiceRoot;
    if (voice->next) voice->next->prev = voice;
    voice->prev = NULL;
    dspStudio[studio].voiceRoot = voice;
  }
  voice->studio = studio;
}
