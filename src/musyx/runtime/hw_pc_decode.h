#ifndef MUSYX_PC_DECODE_H
#define MUSYX_PC_DECODE_H

#include "musyx/synthdata.h"

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
/* Single DSP-ADPCM nibble; round then saturate before updating history. */
s16 salPCDecodeNibble(u8 nibble, u8 ps, const s16 coefficients[8][2], s16* yn1, s16* yn2);

#endif
