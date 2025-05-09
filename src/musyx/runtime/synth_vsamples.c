#include "musyx/musyx.h"

#include "musyx/assert.h"
#include "musyx/hardware.h"
#include "musyx/macros.h"
#include "musyx/seq.h"
#include "musyx/snd.h"
#include "musyx/version.h"
#include "musyx/voice.h"

#if MUSY_TARGET == MUSY_TARGET_DOLPHIN
#include <dolphin/os.h>
#endif

VS vs;

void vsInit() {
  u32 i;
  vs.numBuffers = 0;
  for (i = 0; i < 64; i++) {
    vs.voices[i] = 0xFF;
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
      if (vs.streamBuffer[i].state != 0 && vs.streamBuffer[i].info.instID == instID) {
        break;
      }
    }
  } while (i != vs.numBuffers);

  return instID;
}

u8 vsAllocateBuffer() {
  u8 i;

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state != 0) {
      continue;
    }
    vs.streamBuffer[i].state = 1;
    vs.streamBuffer[i].last = 0;
    return i;
  }

  return 0xFF;
}

void vsFreeBuffer(u8 bufferIndex) {
  vs.streamBuffer[bufferIndex].state = 0;
  vs.voices[vs.streamBuffer[bufferIndex].voice] = 0xFF;
}

u32 vsSampleStartNotify(
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    u32 voiceID
#else
    u8 voice
#endif
) {
  u8 sb; // r29
  u8 i;  // r28
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  u8 voice = voiceID;
#endif
  size_t addr; // r27

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state != 0 && vs.streamBuffer[i].voice == voice) {
      vsFreeBuffer(i);
    }
  }

  sb = vs.voices[voice] = vsAllocateBuffer();
  if (sb != 0xFF) {
    addr = aramGetStreamBufferAddress(vs.voices[voice], 0);
    hwSetVirtualSampleLoopBuffer(voice, (void*)addr, vs.bufferLength);
    vs.streamBuffer[sb].info.smpID = hwGetSampleID(voice);
    vs.streamBuffer[sb].info.instID = vsNewInstanceID();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    if ((vs.streamBuffer[sb].info.vid = vidGetPublicId(voiceID)) != -1) {
      vs.streamBuffer[sb].info.seqID = seqGetInstanceForVoice(vs.streamBuffer[sb].info.vid);
    } else {
      vs.streamBuffer[sb].info.seqID = -1;
    }
    vs.streamBuffer[sb].info.data.start.extraData = hwGetSampleExtraData(voice);
#endif
    vs.streamBuffer[sb].smpType = hwGetSampleType(voice);
    vs.streamBuffer[sb].voice = voice;
    if (vs.callback != NULL && (MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 1)
                                    ? TRUE
                                    : vs.callback(0, &vs.streamBuffer[sb].info) == 0)) {
#if MUSY_VERSION <= MUSY_VERSION_CHECK(2, 0, 1)
      vs.callback(0, &vs.streamBuffer[sb].info);
#endif
      return (vs.streamBuffer[sb].info.instID << 8) | voice;
    }
    hwSetVirtualSampleLoopBuffer(voice, 0, 0);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    vsFreeBuffer(sb);
#endif
  } else {
    hwSetVirtualSampleLoopBuffer(voice, 0, 0);
  }

  return 0xFFFFFFFF;
}

void vsSampleEndNotify(u32 pubID) {
  u8 sb;
  u8 voice;

  if (pubID != 0xFFFFFFFF) {
    u8 id = (u8)pubID;
    sb = vs.voices[id];
    if (sb != 0xFF) {
      if (vs.streamBuffer[sb].info.instID == ((pubID >> 8) & 0xFFFF)) {
        if (vs.callback != NULL) {
          vs.callback(2, &vs.streamBuffer[sb].info);
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
    case 5: {
      u32 off = (sb->last / 14) * 8;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = cpos - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(1, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case 6: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = cpos - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(1, &sb->info)) != 0) {
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
    case 5: {
      u32 off = (sb->last / 14) * 8;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(1, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case 6: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = 0;
      if ((len = vs.callback(1, &sb->info)) != 0) {
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
    case 5: {
      u32 off = (sb->last / 14) * 8;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = cpos;
      if ((len = vs.callback(1, &sb->info)) != 0) {
        sb->last = (sb->last + len) % vs.bufferLength;
      }
    } break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case 6: {
      u32 off = sb->last * 2;
      sb->info.data.update.off1 = off;
      sb->info.data.update.len1 = vs.bufferLength - sb->last;
      sb->info.data.update.off2 = 0;
      sb->info.data.update.len2 = cpos;
      if ((len = vs.callback(1, &sb->info)) != 0) {
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

  for (i = 0; i < 64; ++i) {
    if (vs.voices[i] != 0xFF && hwGetVirtualSampleState(i) != 0) {
      sb = &vs.streamBuffer[vs.voices[i]];
      realCPos = hwGetPos(i);
      if (sb->smpType == 5) {
        cpos = (realCPos / 14) * 14;
      } else {
        cpos = realCPos;
      }

      switch (sb->state) {
      case 1:
        vsUpdateBuffer(sb, cpos);
        break;
      case 2:
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
      case 3:
#endif
        if (((sb->info.instID << 8) | sb->voice) == hwGetVirtualSampleID(sb->voice)) {
          vsUpdateBuffer(sb, cpos);

          if (realCPos >= sb->finalLast) {
            sb->finalGoodSamples -= (realCPos - sb->finalLast);
          } else {
            sb->finalGoodSamples -= (vs.bufferLength - (sb->finalLast - realCPos));
          }

          sb->finalLast = realCPos;
          nextSamples = (synthVoice[sb->voice].curPitch * 160 + 0xFFF) / 4096;
          if ((s32)nextSamples > (s32)sb->finalGoodSamples) {
            if (!hwVoiceInStartup(sb->voice)) {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
              if (sb->state == 2) {
                hwBreak(sb->voice);
                macSampleEndNotify(&synthVoice[sb->voice]);
              } else {
                voiceKill(sb->voice);
              }
#else
              hwBreak(sb->voice);
#endif
            }

            sb->state = 0;
            vs.voices[sb->voice] = 0xff;
          }
        } else {
          sb->state = 0;
          vs.voices[sb->voice] = 0xff;
        }
        break;
      }
    }
  }
}

bool sndVirtualSampleAllocateBuffers(u8 numInstances, u32 numSamples, u32 flags) {
  s32 i;   // r31
  u32 len; // r28
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");
  MUSY_ASSERT_MSG(numInstances <= 64, "Parameter exceeded maximum number of instances allowable");

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
    vs.bufferLength = (len / 8) * 14;
  }
#else
  vs.bufferLength = (len / 8) * 14;
#endif

  for (i = 0; i < vs.numBuffers; ++i) {
    if ((vs.streamBuffer[i].hwId = aramAllocateStreamBuffer(len)) == 0xFF) {
      i--;
      while (i > 0) {
        aramFreeStreamBuffer(vs.streamBuffer[i].hwId);
        --i;
      }
      hwEnableIrq();
      return 0;
    }
    vs.streamBuffer[i].state = 0;
    vs.voices[vs.streamBuffer[i].voice] = 0xFF;
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

void sndVirtualSampleSetCallback(u32 (*callback)(u8 reason, const SND_VIRTUALSAMPLE_INFO* info)) {
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");
  vs.callback = callback;
}

void vsARAMDMACallback(size_t user) {
  if (vs.callback == NULL) {
    return;
  }

  vs.callback(3, &((VS_BUFFER*)user)->info);
}

void sndVirtualSampleARAMUpdate(SND_INSTID instID, void* base, u32 off1, u32 len1, u32 off2,
                                u32 len2) {
  u8 i;
  MUSY_ASSERT_MSG(sndActive, "Sound system is not initialized.");

  hwDisableIrq();

  for (i = 0; i < vs.numBuffers; ++i) {
    if (vs.streamBuffer[i].state == 0 || vs.streamBuffer[i].info.instID != instID) {
      continue;
    }

    switch ((s32)vs.streamBuffer[i].smpType) {
    case 5:
      off1 = (off1 / 14) * 8;
      len1 = ((len1 + 13) / 14) * 8;
      off2 = (off2 / 14) * 8;
      len2 = ((len2 + 13) / 14) * 8;
      break;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    case 6:
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
                    (u32)&vs.streamBuffer[i]);
    }
    if (len2 != 0) {
      hwFlushStream(base, off2, len2, vs.streamBuffer[i].hwId, vsARAMDMACallback,
                    (u32)&vs.streamBuffer[i]);
    }

    if (vs.streamBuffer[i].smpType == 5) {
#if MUSY_TARGET == MUSY_TARGET_DOLPHIN
      hwSetStreamLoopPS(vs.streamBuffer[i].voice, *(u32*)(OSCachedToUncached(base)) >> 24);
#elif MUSY_TARGET == MUSY_TARGET_PC
      hwSetStreamLoopPS(vs.streamBuffer[i].voice, *(u32*)(base) >> 24);
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
    if (vs.streamBuffer[i].state == 0 || vs.streamBuffer[i].info.instID != instID) {
      continue;
    }

    stream = &vs.streamBuffer[i];
    cpos = hwGetPos(i);

    if (stream->last < cpos) {
      stream->finalGoodSamples = vs.bufferLength - (cpos - stream->last);
    } else {
      stream->finalGoodSamples = stream->last - cpos;
    }

    stream->finalLast = cpos;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 0)
    stream->state = sampleEndedNormally != 0 ? 2 : 3;
#else
    stream->state = 2;
#endif
    break;
#else
    if ((vs.streamBuffer[i].state != 0) && (vs.streamBuffer[i].info.instID == instID)) {
      vs.streamBuffer[i].finalLast = hwGetPos(vs.streamBuffer[i].voice);
      vs.streamBuffer[i].finalGoodSamples = numLastGoodSamples;
      vs.streamBuffer[i].state = sampleEndedNormally ? 2 : 3;
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
    if ((vs.streamBuffer[i].state != 0) && (vs.streamBuffer[i].info.instID == instID)) {
      *aramAddr = hwGetStreamARAMAddr(vs.streamBuffer[i].hwId);
      ret = 1;
      break;
    }
  }
  hwEnableIrq();
  return ret;
}
#endif
