#ifndef MUSYX_PC_INTERNAL_H
#define MUSYX_PC_INTERNAL_H

#include "musyx/pc.h"
#include "musyx/sal.h"

#define SAL_PC_MIN_RATE 8000
#define SAL_PC_MAX_RATE 96000
#define SAL_PC_MAX_FRAMES (SAL_PC_MAX_RATE / 200)
#define SAL_PC_MAX_CHANNELS 8
#define SAL_PC_STUDIO_BYTES (8 * 3 * SAL_PC_MAX_FRAMES * sizeof(s32))

/* B(k) = ceil(k * rate / 1000), with no accumulated rounding error. Computing
 * quotient and remainder separately also avoids overflowing tick * rate. */
static inline u64 salPCBoundary(u64 tick, u32 rate) {
  return (tick / 1000) * rate + ((tick % 1000) * rate + 999) / 1000;
}

void salPCResetOutput(SND_SOME_CALLBACK callback);
u32 salPCMixRate(void);
u32 salPCChannels(void);
bool salPCExternalEnter(void);
bool salPCExternalLeave(void);
void salPCBeginCycle(u64 tick);

/* Causal rate adapter for legacy 32 kHz AUX blocks.
 * Each span is a logical 5 ms cycle, whose first sample is B(tick, rate).
 * history holds the preceding 32 source samples. Latency: 16 source samples.
 * Persistent sample-grid phase comes from integer control time, never block size. */
void salPCConvertCycle(s32* output, u32 outputFrames, u32 outputRate, u64 outputTick,
                      const s32* input, u32 inputFrames, u32 inputRate, u64 inputTick,
                      s32 history[32]);
const s32* salPCRateCoefficients(u32 inputRate, u32 outputRate, u32 fraction);
/* Same-rate studio delays only need fractional-grid alignment. */
void salPCConvertStudioCycle(s32* output, u32 outputFrames, u32 outputRate, u64 outputTick,
                            const s32* input, u32 inputFrames, u32 inputRate, u64 inputTick,
                            s32 history[2]);

void salPCExitStreams(void);

#endif
