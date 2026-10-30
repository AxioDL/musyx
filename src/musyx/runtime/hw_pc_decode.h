#ifndef MUSYX_PC_DECODE_H
#define MUSYX_PC_DECODE_H

#include "musyx/synthdata.h"

static inline bool salPCIsVirtualSample(u8 type) {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  if (type == SAMPLE_TYPE_PCM16_VIRTUAL)
    return true;
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
  return type == SAMPLE_TYPE_ADPCM_VIRTUAL;
#else
  return false;
#endif
}

static inline bool salPCIsPCM16(u8 type) {
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  if (type == SAMPLE_TYPE_PCM16_VIRTUAL)
    return true;
#endif
  return type == SAMPLE_TYPE_PCM16;
}

static inline bool salPCValidSampleLoop(u32 length, u32 loop, u32 loopLength, u8 type) {
  u64 extent = length;
  /* ADPCM uploads include the entire final block. Shipped Echoes loops use
   * samples beyond the nominal length within that block, as the DSP allows. */
  if (type == SAMPLE_TYPE_ADPCM || type == SAMPLE_TYPE_ADPCM_PLUS ||
      type == SAMPLE_TYPE_ADPCM_STREAM || type == SAMPLE_TYPE_ADPCM_VIRTUAL)
    extent = (extent + SND_STREAM_ADPCM_BLKSIZE - 1) / SND_STREAM_ADPCM_BLKSIZE *
             SND_STREAM_ADPCM_BLKSIZE;
  return loop <= extent && loopLength <= extent - loop;
}

/* Native cursor, measured in decoded source samples. It never contains a
 * packed DSP address or a pointer truncated to 32 bits. */
typedef struct MusyPCMReader {
  u32 position;
  s16 yn1, yn2;
  u8 predictorScale;
  bool useInitialPS;
  bool ended;
} MusyPCMReader;

bool salPCInitReader(MusyPCMReader* reader, const SAMPLE_INFO* sample);
s16 salPCReadSample(MusyPCMReader* reader, const SAMPLE_INFO* sample, u8 streamLoopPS);
/* Single DSP-ADPCM nibble. Round then saturate before updating history. */
s16 salPCDecodeNibble(u8 nibble, u8 ps, const s16 coefficients[8][2], s16* yn1, s16* yn2);

#endif
