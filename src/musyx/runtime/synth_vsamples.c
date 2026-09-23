#include "musyx/musyx.h"

#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/macros.h"
#include "musyx/seq.h"
#include "musyx/snd.h"
#include "musyx/synthdata.h"
#include "musyx/version.h"
#include "musyx/voice.h"
#if MUSY_TARGET == MUSY_TARGET_PC
#include "hw_pc_internal.h"
#endif

#if MUSY_TARGET == MUSY_TARGET_DOLPHIN
#include <dolphin/os.h>
#endif

VS vs;

void vsInit() {
  u32 i;
  vs.numBuffers = 0;
  for (i = 0; i < SYNTH_MAX_VOICES; i++) {
    vs.voices[i] = VS_BUFFER_NONE;
  }

  vs.nextInstID = 0;
  vs.callback = NULL;
}

u16 vsNewInstanceID() {
  u8 i;       // r31
  u16 instID; // r29
  do {
    instID = vs.nextInstID++;
    for (i = 0; i < vs.numBuffers; ++i) {
      if (vs.streamBuffer[i].state != VS_STATE_FREE && vs.streamBuffer[i].info.instID == instID) {
        break;
      }
    }
  } while (i != vs.numBuffers);

  return instID;
}

u8 vsAllocateBuffer() {
  u8 i;

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state != VS_STATE_FREE) {
      continue;
    }
    vs.streamBuffer[i].state = VS_STATE_STREAMING;
    vs.streamBuffer[i].last = 0;
    return i;
  }

  return VS_BUFFER_NONE;
}

void vsFreeBuffer(u8 bufferIndex) {
  vs.streamBuffer[bufferIndex].state = VS_STATE_FREE;
  vs.voices[vs.streamBuffer[bufferIndex].voice] = VS_BUFFER_NONE;
}

u32 vsSampleStartNotify(
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    u32 voiceID
#else
    u8 voice
#endif
) {
  u8 sb;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
  u8 voice = voiceID & 0xFF;
  u8 hwVoice;
#endif
  u8 i;
#if MUSY_VERSION == MUSY_VERSION_CHECK(2, 0, 2)
  u8 voice = voiceID;
#endif
  size_t addr;

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state != VS_STATE_FREE && vs.streamBuffer[i].voice == voice) {
      vsFreeBuffer(i);
    }
  }

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
  sb = vsAllocateBuffer();
  hwVoice = voice;
  vs.voices[hwVoice] = sb;
#else
  sb = vs.voices[voice] = vsAllocateBuffer();
#endif
  if (sb != VS_BUFFER_NONE) {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
    addr = aramGetStreamBufferAddress(vs.voices[hwVoice], 0);
    hwSetVirtualSampleLoopBuffer(hwVoice, (void*)addr, vs.bufferLength);
    vs.streamBuffer[sb].info.smpID = hwGetSampleID(hwVoice);
#else
#if MUSY_TARGET == MUSY_TARGET_PC
    addr = aramGetStreamBufferAddress(vs.streamBuffer[sb].hwId, 0);
#else
    addr = aramGetStreamBufferAddress(vs.voices[voice], 0);
#endif
    hwSetVirtualSampleLoopBuffer(voice, (void*)addr, vs.bufferLength);
    vs.streamBuffer[sb].info.smpID = hwGetSampleID(voice);
#endif
    vs.streamBuffer[sb].info.instID = vsNewInstanceID();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    if ((vs.streamBuffer[sb].info.vid = vidGetPublicId(voiceID)) != -1) {
      vs.streamBuffer[sb].info.seqID = seqGetInstanceForVoice(vs.streamBuffer[sb].info.vid);
    } else {
      vs.streamBuffer[sb].info.seqID = -1;
    }
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
    vs.streamBuffer[sb].info.data.start.extraData = hwGetSampleExtraData(hwVoice);
#else
    vs.streamBuffer[sb].info.data.start.extraData = hwGetSampleExtraData(voice);
#endif
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
    vs.streamBuffer[sb].smpType = hwGetSampleType(hwVoice);
#else
    vs.streamBuffer[sb].smpType = hwGetSampleType(voice);
#endif
    vs.streamBuffer[sb].voice = voice;
    if (vs.callback != NULL && (MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 1)
                                    ? TRUE
                                    : vs.callback(SND_VIRTUALSAMPLE_REASON_INIT, &vs.streamBuffer[sb].info) == 0)) {
#if MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 1)
      vs.callback(SND_VIRTUALSAMPLE_REASON_INIT, &vs.streamBuffer[sb].info);
#endif
      return (vs.streamBuffer[sb].info.instID << 8) | voice;
    }
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
    hwSetVirtualSampleLoopBuffer(hwVoice, 0, 0);
#else
    hwSetVirtualSampleLoopBuffer(voice, 0, 0);
#endif
#if MUSY_TARGET == MUSY_TARGET_PC || MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    vsFreeBuffer(sb);
#endif
  } else {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 3)
    hwSetVirtualSampleLoopBuffer(hwVoice, 0, 0);
#else
    hwSetVirtualSampleLoopBuffer(voice, 0, 0);
#endif
  }

  return 0xFFFFFFFF;
}

void vsSampleEndNotify(u32 pubID) {
  u8 sb;
  u8 voice;

  if (pubID != 0xFFFFFFFF) {
    u8 id = (u8)pubID;
    sb = vs.voices[id];
    if (sb != VS_BUFFER_NONE) {
      if (vs.streamBuffer[sb].info.instID == ((pubID >> 8) & 0xFFFF)) {
        if (vs.callback != NULL) {
          vs.callback(SND_VIRTUALSAMPLE_REASON_STOP, &vs.streamBuffer[sb].info);
        }
        vsFreeBuffer(sb);
      }
    }
  }
}

void vsUpdateBuffer(struct VS_BUFFER* sb, unsigned long cpos) {
  u32 len;
  if (sb->last == cpos) {
    return;
  }
  if ((s32)sb->last < cpos) {
    switch (sb->smpType) {
    case SAMPLE_TYPE_ADPCM_VIRTUAL: {
      u32 off = (sb->last / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = cpos - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case SAMPLE_TYPE_PCM16_VIRTUAL: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = cpos - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
        return;
      }
    } break;
#endif
    default:
      break;
    }
  } else if (cpos == 0) {
    switch (sb->smpType) {
    case SAMPLE_TYPE_ADPCM_VIRTUAL: {
      u32 off = (sb->last / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case SAMPLE_TYPE_PCM16_VIRTUAL: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
        return;
      }
    } break;
#endif
    default:
      break;
    }
  } else {
    switch (sb->smpType) {
    case SAMPLE_TYPE_ADPCM_VIRTUAL: {
      u32 off = (sb->last / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = cpos;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case SAMPLE_TYPE_PCM16_VIRTUAL: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = cpos;
      if ((len = vs.callback(SND_VIRTUALSAMPLE_REASON_UPDATE, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#endif
    default:
      break;
    }
  }
}

void vsSampleUpdates() {
  u32 i;           // r29
  u32 cpos;        // r27
  u32 realCPos;    // r28
  VS_BUFFER* sb;   // r31
  u32 nextSamples; // r26

  if (vs.callback == NULL) {
    return;
  }

  for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
    if (vs.voices[i] != VS_BUFFER_NONE && hwGetVirtualSampleState(i) != 0) {
      sb = &vs.streamBuffer[vs.voices[i]];
      realCPos = hwGetPos(i);
      if (sb->smpType == SAMPLE_TYPE_ADPCM_VIRTUAL) {
        cpos = (realCPos / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKSIZE;
      } else {
        cpos = realCPos;
      }

      switch (sb->state) {
      case VS_STATE_STREAMING:
        vsUpdateBuffer(sb, cpos);
        break;
      case VS_STATE_DRAINING:
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
      case VS_STATE_DRAINING_ABORT:
#endif
        if (((sb->info.instID << 8) | sb->voice) == hwGetVirtualSampleID(sb->voice)) {
          vsUpdateBuffer(sb, cpos);

          if (realCPos >= sb->finalLast) {
            sb->finalGoodSamples -= (realCPos - sb->finalLast);
          } else {
            sb->finalGoodSamples -= (vs.bufferLength - (sb->finalLast - realCPos));
          }

          sb->finalLast = realCPos;
#if MUSY_TARGET == MUSY_TARGET_PC
          nextSamples = ((u64)synthVoice[sb->voice].curPitch * ((salPCMixRate() + 199) / 200) + 0xfff) / 4096;
#else
          nextSamples = (synthVoice[sb->voice].curPitch * 160 + 0xFFF) / 4096;
#endif
          if ((s32)nextSamples > (s32)sb->finalGoodSamples) {
            if (!hwVoiceInStartup(sb->voice)) {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
              if (sb->state == VS_STATE_DRAINING) {
                hwBreak(sb->voice);
                macSampleEndNotify(&synthVoice[sb->voice]);
              } else {
                voiceKill(sb->voice);
              }
#else
              hwBreak(sb->voice);
#endif
            }

            sb->state = VS_STATE_FREE;
            vs.voices[sb->voice] = VS_BUFFER_NONE;
          }
        } else {
          sb->state = VS_STATE_FREE;
          vs.voices[sb->voice] = VS_BUFFER_NONE;
        }
        break;
      }
    }
  }
}

#if MUSY_TARGET != MUSY_TARGET_PC
bool sndVirtualSampleAllocateBuffers(u8 numInstances, u32 numSamples, u32 flags) {
  s32 i;   // r31
  u32 len; // r28
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");
  MUSY_ASSERT_MSG(numInstances <= VS_MAX_BUFFERS, "Parameter exceeded maximum number of instances allowable");

  hwDisableIrq();
  vs.numBuffers = numInstances;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  len = sndStreamAllocLength(numSamples, (flags & 1) == 0);
#else
  len = sndStreamAllocLength(numSamples, 1);
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  if (flags & 1) {
    vs.bufferLength = len >> 1;
  } else {
    vs.bufferLength = (len / SND_STREAM_ADPCM_BLKBYTES) * SND_STREAM_ADPCM_BLKSIZE;
  }
#else
  vs.bufferLength = (len / SND_STREAM_ADPCM_BLKBYTES) * SND_STREAM_ADPCM_BLKSIZE;
#endif

  for (i = 0; i < vs.numBuffers; ++i) {
    if ((vs.streamBuffer[i].hwId = aramAllocateStreamBuffer(len)) == HW_STREAM_BUFFER_INVALID) {
      i--;
      while (i > 0) {
        aramFreeStreamBuffer(vs.streamBuffer[i].hwId);
        --i;
      }
      hwEnableIrq();
      return 0;
    }
    vs.streamBuffer[i].state = VS_STATE_FREE;
    vs.voices[vs.streamBuffer[i].voice] = VS_BUFFER_NONE;
  }

  hwEnableIrq();
  return 1;
}

void sndVirtualSampleFreeBuffers() {
  u8 i; // r31
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");

  for (i = 0; i < vs.numBuffers; ++i) {
    aramFreeStreamBuffer(vs.streamBuffer[i].hwId);
  }

  vs.numBuffers = 0;
}

#endif

void sndVirtualSampleSetCallback(u32 (*callback)(u8 reason, const SND_VIRTUALSAMPLE_INFO* info)) {
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");
  vs.callback = callback;
}

void vsARAMDMACallback(size_t user) {
  if (vs.callback == NULL) {
    return;
  }

  vs.callback(SND_VIRTUALSAMPLE_REASON_ARAMDMADONE, &((VS_BUFFER*)user)->info);
}

void sndVirtualSampleARAMUpdate(SND_INSTID instID, void* base, u32 off1, u32 len1, u32 off2,
                                u32 len2) {
  u8 i;
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");

  hwDisableIrq();

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state == VS_STATE_FREE || vs.streamBuffer[i].info.instID != instID) {
      continue;
    }

    switch ((s32)vs.streamBuffer[i].smpType) {
    case SAMPLE_TYPE_ADPCM_VIRTUAL:
      off1 = (off1 / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      len1 = ((len1 + 13) / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      off2 = (off2 / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      len2 = ((len2 + 13) / SND_STREAM_ADPCM_BLKSIZE) * SND_STREAM_ADPCM_BLKBYTES;
      break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case SAMPLE_TYPE_PCM16_VIRTUAL:
      off1 *= 2;
      len1 *= 2;
      off2 *= 2;
      len2 *= 2;
      break;
#endif
    default:
      break;
    }

    if (len1 != 0) {
      hwFlushStream(base, off1, len1, vs.streamBuffer[i].hwId, vsARAMDMACallback,
                    (MUSY_HOST_USER)&vs.streamBuffer[i]);
    }
    if (len2 != 0) {
      hwFlushStream(base, off2, len2, vs.streamBuffer[i].hwId, vsARAMDMACallback,
                    (MUSY_HOST_USER)&vs.streamBuffer[i]);
    }

    if (vs.streamBuffer[i].smpType == SAMPLE_TYPE_ADPCM_VIRTUAL) {
#if MUSY_TARGET == MUSY_TARGET_DOLPHIN
      hwSetStreamLoopPS(vs.streamBuffer[i].voice, *(u32*)(OSCachedToUncached(base)) >> 24);
#elif MUSY_TARGET == MUSY_TARGET_PC
      hwSetStreamLoopPS(vs.streamBuffer[i].voice, *(const u8*)base);
#endif
    }
    break;
  }

  hwEnableIrq();
}

void sndVirtualSampleEndPlayback(SND_INSTID instID, bool sampleEndedNormally
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
                                 ,
                                 u32 numLastGoodSamples
#endif
) {
  u8 i;              // r30
  VS_BUFFER* stream; // r31
  u32 cpos;          // r28

  hwDisableIrq();

  for (i = 0; i < vs.numBuffers; ++i) {
#if MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 1)
    if (vs.streamBuffer[i].state == VS_STATE_FREE || vs.streamBuffer[i].info.instID != instID) {
      continue;
    }

    stream = &vs.streamBuffer[i];
#if MUSY_TARGET == MUSY_TARGET_PC
    cpos = hwGetPos(stream->voice);
#else
    cpos = hwGetPos(i);
#endif

    if (stream->last < cpos) {
      stream->finalGoodSamples = vs.bufferLength - (cpos - stream->last);
    } else {
      stream->finalGoodSamples = stream->last - cpos;
    }

    stream->finalLast = cpos;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
    stream->state = sampleEndedNormally != 0 ? VS_STATE_DRAINING : VS_STATE_DRAINING_ABORT;
#else
    stream->state = VS_STATE_DRAINING;
#endif
    break;
#else
    if ((vs.streamBuffer[i].state != VS_STATE_FREE) && (vs.streamBuffer[i].info.instID == instID)) {
      vs.streamBuffer[i].finalLast = hwGetPos(vs.streamBuffer[i].voice);
      vs.streamBuffer[i].finalGoodSamples = numLastGoodSamples;
      vs.streamBuffer[i].state = sampleEndedNormally ? VS_STATE_DRAINING : VS_STATE_DRAINING_ABORT;
      break;
    }
#endif
  }
  hwEnableIrq();
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
s32 sndVirtualSampleGetARAMAddress(u16 instID, s32* aramAddr) {
  u8 i;    // r31
  u32 ret; // r30

  ret = 0;
  hwDisableIrq();
  for (i = 0; i < vs.numBuffers; ++i) {
    if ((vs.streamBuffer[i].state != VS_STATE_FREE) && (vs.streamBuffer[i].info.instID == instID)) {
      *aramAddr = hwGetStreamARAMAddr(vs.streamBuffer[i].hwId);
      ret = 1;
      break;
    }
  }
  hwEnableIrq();
  return ret;
}
#endif
