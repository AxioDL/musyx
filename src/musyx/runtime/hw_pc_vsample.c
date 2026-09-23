#include "musyx/hardware.h"
#include "musyx/voice.h"
#include <string.h>

bool sndVirtualSampleAllocateBuffers(u8 instances, u32 samples, u32 flags) {
  if (!sndActive || instances > VS_MAX_BUFFERS || !samples || samples > UINT32_MAX - 64 || flags)
    return false;
  hwDisableIrq();
  if (vs.numBuffers) { hwEnableIrq(); return false; }
  u32 length = sndStreamAllocLength(samples, SND_STREAM_ADPCM);
  u8 count = 0;
  for (; count < instances; ++count) {
    u8 id = aramAllocateStreamBuffer(length);
    if (id == HW_STREAM_BUFFER_INVALID) break;
    memset(&vs.streamBuffer[count], 0, sizeof(vs.streamBuffer[count]));
    vs.streamBuffer[count].hwId = id;
    vs.streamBuffer[count].voice = VS_VOICE_NONE;
  }
  if (count != instances) {
    while (count) aramFreeStreamBuffer(vs.streamBuffer[--count].hwId);
    hwEnableIrq();
    return false;
  }
  vs.numBuffers = instances;
  vs.bufferLength = (length / SND_STREAM_ADPCM_BLKBYTES) * SND_STREAM_ADPCM_BLKSIZE;
  memset(vs.voices, VS_BUFFER_NONE, sizeof(vs.voices));
  hwEnableIrq();
  return true;
}

void sndVirtualSampleFreeBuffers(void) {
  if (!sndActive) return;
  hwDisableIrq();
  for (u32 i = 0; i < vs.numBuffers; ++i) {
    VS_BUFFER* buffer = &vs.streamBuffer[i];
    if (buffer->state != VS_STATE_FREE && buffer->voice < SYNTH_MAX_VOICES) {
      voiceKill(buffer->voice);
      hwOff(buffer->voice);
    }
    aramFreeStreamBuffer(buffer->hwId);
    memset(buffer, 0, sizeof(*buffer));
  }
  vs.numBuffers = 0;
  memset(vs.voices, VS_BUFFER_NONE, sizeof(vs.voices));
  hwEnableIrq();
}
