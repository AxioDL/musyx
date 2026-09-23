#include "hw_pc_decode.h"
#include <string.h>

s16 salPCDecodeNibble(u8 nibble, u8 ps, const s16 coefficients[8][2], s16* yn1, s16* yn2) {
  s32 value = (nibble & 8) ? (s32)(nibble & 15) - 16 : (nibble & 15);
  u32 predictor = (ps >> 4) & 7;
  s64 sum = (s64)value * (1u << (ps & 15)) * 2048 + 1024;
  if (coefficients)
    sum += (s64)coefficients[predictor][0] * *yn1 + (s64)coefficients[predictor][1] * *yn2;
  /* Arithmetic floor is explicit, including on hosts with a logical signed shift. */
  sum = sum < 0 ? -((-sum + 2047) / 2048) : sum / 2048;
  s16 result = sum < -32768 ? -32768 : sum > 32767 ? 32767 : (s16)sum;
  *yn2 = *yn1;
  *yn1 = result;
  return result;
}

bool salPCInitReader(MusyPCMReader* reader, const SAMPLE_INFO* sample) {
  memset(reader, 0, sizeof(*reader));
  if (!sample->addr || !sample->length || sample->compType > 5 ||
      sample->loop > sample->length || sample->loopLength > sample->length - sample->loop) {
    reader->ended = true;
    return false;
  }
  const DSPADPCMplusInfo* adpcm = sample->extraData;
  switch (sample->compType) {
  case 0:
  case 4:
  case 5:
    reader->predictorScale = adpcm ? adpcm->initialPS : ((const u8*)sample->addr)[0];
    reader->useInitialPS = true;
    break;
  case 1: {
    u32 block = (sample->offset + 13) / 14;
    reader->position = block * 14;
    if (reader->position >= sample->length || !adpcm) {
      reader->ended = true;
      return false;
    }
    reader->yn1 = adpcm->blk[block].Y1;
    reader->yn2 = adpcm->blk[block].Y0;
    reader->predictorScale = adpcm->blk[block].PS;
    reader->useInitialPS = true;
    break;
  }
  default:
    reader->position = sample->offset;
    break;
  }
  if (reader->position >= sample->length) {
    reader->ended = true;
    return false;
  }
  return true;
}

s16 salPCReadSample(MusyPCMReader* reader, const SAMPLE_INFO* sample, u8 streamLoopPS) {
  if (reader->ended)
    return 0;
  const DSPADPCMplusInfo* adpcm = sample->extraData;
  const u32 end = sample->loopLength ? sample->loop + sample->loopLength : sample->length;
  if (reader->position >= end) {
    if (!sample->loopLength) {
      reader->ended = true;
      return 0;
    }
    reader->position = sample->loop;
    if (sample->compType == 4 || sample->compType == 5) {
      /* Stream rings retain history from the preceding data, unlike static loops. */
      reader->predictorScale = streamLoopPS;
    } else if (adpcm) {
      reader->yn1 = adpcm->loopY1;
      reader->yn2 = adpcm->loopY0;
      reader->predictorScale = adpcm->loopPS;
    }
    reader->useInitialPS = true;
  }
  const u32 position = reader->position++;
  switch (sample->compType) {
  case 0:
  case 1:
  case 4:
  case 5: {
    const u8* block = (const u8*)sample->addr + (position / 14) * 8;
    if (!reader->useInitialPS && position % 14 == 0)
      reader->predictorScale = block[0];
    reader->useInitialPS = false;
    const u32 within = position % 14;
    u8 nibble = (block[1 + within / 2] >> (within % 2 ? 0 : 4)) & 15;
    return salPCDecodeNibble(nibble, reader->predictorScale,
                            adpcm ? adpcm->coefTab : NULL, &reader->yn1, &reader->yn2);
  }
  case 2: {
    /* Static PCM16 is normalized at upload. Native stream PCM16 is already host order. */
    s16 result;
    memcpy(&result, (const u8*)sample->addr + position * sizeof(result), sizeof(result));
    return result;
  }
  case 3:
    return (s16)((s32)((const s8*)sample->addr)[position] * 256);
  default:
    reader->ended = true;
    return 0;
  }
}
