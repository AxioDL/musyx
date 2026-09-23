#include "hw_pc_internal.h"

void salPCConvertStudioCycle(s32 *output, u32 outputFrames, u32 outputRate, u64 outputTick,
                             const s32 *input, u32 inputFrames, u32 inputRate, u64 inputTick,
                             s32 history[2]) {
  s64 sourcePhase = (1000 - (inputTick % 1000) * inputRate % 1000) % 1000;
  s64 destPhase = (1000 - (outputTick % 1000) * outputRate % 1000) % 1000;
  const s64 denominator = (s64)outputRate * 1000;
  s64 position = destPhase * inputRate - sourcePhase * outputRate;
  for (u32 frame = 0; frame < outputFrames; ++frame, position += (s64)inputRate * 1000) {
    s64 index =
        position < 0 ? -((-position + denominator - 1) / denominator) : position / denominator;
    s64 fraction = position - index * denominator;
    s32 a = index <= 0 ? history[index < 0 ? 0 : 1] : input[MIN((u32)index - 1, inputFrames - 1)];
    s32 b = index < 0 ? history[1] : input[MIN((u32)index, inputFrames - 1)];
    output[frame] = (s32)((s64)a + ((s64)b - a) * fraction / denominator);
  }
  history[0] = inputFrames > 1 ? input[inputFrames - 2] : history[1];
  history[1] = input[inputFrames - 1];
}

void salPCConvertCycle(s32 *output, u32 outputFrames, u32 outputRate, u64 outputTick,
                       const s32 *input, u32 inputFrames, u32 inputRate, u64 inputTick,
                       s32 history[32]) {
  const s64 sourcePhase = (s64)(1000 - (inputTick % 1000) * inputRate % 1000) % 1000;
  const s64 destPhase = (s64)(1000 - (outputTick % 1000) * outputRate % 1000) % 1000;
  const s64 denominator = (s64)outputRate * 1000;
  s64 position = destPhase * inputRate - sourcePhase * outputRate;
  for (u32 frame = 0; frame < outputFrames; ++frame, position += (s64)inputRate * 1000) {
    s64 index =
        position < 0 ? -((-position + denominator - 1) / denominator) : position / denominator;
    u32 fraction = (u32)((position - index * denominator) * 65536 / denominator);
    const s32 *filter = salPCRateCoefficients(inputRate, outputRate, fraction);
    s64 sum = 0;
    for (u32 tap = 0; tap < 32; ++tap) {
      s64 offset = index - 31 + tap;
      s32 value = offset < 0 ? history[32 + offset] : input[MIN((u32)offset, inputFrames - 1)];
      sum += (s64)value * filter[tap];
    }
    sum >>= 23;
    output[frame] = (s32)CLAMP(sum, INT32_MIN, INT32_MAX);
  }
  /* Every supported 5 ms input cycle has at least 40 frames. */
  for (u32 i = 0; i < 32; ++i)
    history[i] = input[inputFrames - 32 + i];
}
