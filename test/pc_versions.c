#include "hw_pc_assets.h"
#include "hw_pc_decode.h"
#include "musyx/aramdma.h"
#include "musyx/pc.h"
#include "musyx/sal.h"
#include "musyx/vsamples.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                                               \
  do {                                                                                             \
    if (!(value)) {                                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                                  \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
#define ARAM_ARG , NULL
#else
#define ARAM_ARG
#endif

static size_t allocations;
static void* allocate(size_t size) {
  void* result = malloc(size);
  if (result)
    ++allocations;
  return result;
}
static void release(void* ptr) {
  if (ptr) {
    CHECK(allocations != 0);
    --allocations;
    free(ptr);
  }
}

static void testDirectoryConversion(void) {
  SDIR_DATA_INTER* input = calloc(2, sizeof(*input));
  CHECK(input != NULL);
  input[0].id = 42;
  input[0].ref_cnt = 3;
  input[0].offset = 0x12345678;
  input[0].addr = 0xf1234567;
  input[0].header.info = 32000;
  input[0].header.length = SAMPLE_TYPE_PCM16 << 24 | 4;
  input[0].header.loopLength = 4;
  input[0].extraData = 0x87654321;
  input[1].id = 0xffff;
  SDIR_DATA* output = sndConvert32BitSDIRTo64BitSDIR(input);
  CHECK(output != NULL);
  CHECK(output[0].id == 42 && output[0].ref_cnt == 3);
  CHECK(output[0].offset == 0x12345678);
  CHECK((uintptr_t)output[0].addr == 0xf1234567u);
  CHECK(output[0].header.length == (SAMPLE_TYPE_PCM16 << 24 | 4));
  CHECK(output[0].header.loopLength == 4 && output[0].extraData == 0x87654321);
  CHECK(output[1].id == 0xffff);
  free(output);
  CHECK(sndConvert32BitSDIRTo64BitSDIR(NULL) == NULL);
}

static u8 sampleBytes[] = {0x12, 0x34, 0xfe, 0xdc, 0x7f, 0xff, 0x80, 0x00};
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
static void* upload(u32 offset, u32 size) {
  CHECK(offset <= sizeof(sampleBytes) && size <= sizeof(sampleBytes) - offset);
  return sampleBytes + offset;
}
#endif

static void testSamples(u8 type, bool callback) {
  size_t before = allocations;
  SDIR_DATA directory[2] = {0};
  directory[0].id = 7;
  directory[0].header.info = 32000;
  directory[0].header.length = (u32)type << 24 | 4;
  directory[1].id = 0xffff;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
  if (callback)
    hwSetSaveSampleCallback(upload, 32);
#endif
  CHECK(dataInsertSDir(directory, callback ? NULL : sampleBytes));
  CHECK(dataAddSampleReference(7 ARAM_ARG));
  CHECK(dataAddSampleReference(7 ARAM_ARG));
  CHECK(directory[0].addr != sampleBytes && directory[0].ref_cnt == 2);
  salPCFinishSampleUpload();
  SAMPLE_INFO sample;
  CHECK(dataGetSample(7, &sample) == 0);
  MusyPCMReader reader;
  CHECK(salPCInitReader(&reader, &sample));
  CHECK(salPCReadSample(&reader, &sample, 0) == 0x1234);
  CHECK(salPCReadSample(&reader, &sample, 0) == (s16)0xfedc);
  CHECK(salPCReadSample(&reader, &sample, 0) == 32767);
  CHECK(salPCReadSample(&reader, &sample, 0) == -32768);
  CHECK(dataRemoveSampleReference(7 ARAM_ARG));
  CHECK(directory[0].addr != NULL);
  CHECK(dataRemoveSampleReference(7 ARAM_ARG));
  CHECK(directory[0].addr == NULL);
  CHECK(dataRemoveSDir(directory));
  CHECK(allocations == before);
}

static void testRawDirectory(void) {
  const u8 project[] = {0xff, 0xff, 0xff, 0xff};
  u8 directory[34] = {0};
  directory[1] = 7;
  directory[14] = 0x7d;
  directory[16] = SAMPLE_TYPE_PCM16_VIRTUAL;
  directory[19] = 4;
  directory[32] = directory[33] = 0xff;
  SND_PC_GROUP_ASSETS assets = {{project, sizeof(project)},
                                {NULL, 0},
                                {directory, sizeof(directory)},
                                {sampleBytes, sizeof(sampleBytes)}};
  MusyPCGroupData decoded;
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
  CHECK(salPCDecodeGroup(&assets, &decoded, NULL));
  CHECK(decoded.sampleCount == 1 && decoded.directory[0].id == 7);
  CHECK(decoded.directory[0].header.info == 32000);
  CHECK(decoded.directory[0].header.length == (SAMPLE_TYPE_PCM16_VIRTUAL << 24 | 4));
  CHECK(decoded.directory[0].addr == NULL && decoded.directory[0].extraData == 0);
  salPCFreeGroupData(&decoded);
  assets.samples.size = sizeof(sampleBytes) - 1;
#endif
  CHECK(!salPCDecodeGroup(&assets, &decoded, NULL));
}

static void testGroup(void) {
  size_t before = allocations;
  u8 project[48] = {0};
  project[3] = 44;
  project[5] = project[7] = 1;
  for (u32 offset = 8; offset <= 24; offset += 4)
    project[offset + 3] = 36;
  project[15] = 38;
  project[31] = 32;
  project[36] = project[37] = 0xff;
  project[39] = 7;
  project[40] = project[41] = 0xff;
  memset(project + 44, 0xff, 4);
  u8 directory[34] = {0};
  directory[1] = 7;
  directory[14] = 0x7d;
  directory[16] = SAMPLE_TYPE_PCM16;
  directory[19] = 4;
  directory[32] = directory[33] = 0xff;
  CHECK(sndPushGroup(project, 1, sampleBytes, directory, NULL));
  SAMPLE_INFO sample;
  CHECK(dataGetSample(7, &sample) == 0);
  CHECK(sample.addr != sampleBytes && sample.length == 4);
  CHECK(sndPopGroup());
  CHECK(dataGetSample(7, &sample) == -1);
  CHECK(allocations == before);
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
static unsigned completions;
static void transferDone(size_t user) {
  CHECK(user == (uintptr_t)&completions);
  ++completions;
}
#endif

static void testStreams(void) {
  u8 first = hwInitStream(64), second = hwInitStream(64);
  CHECK(first != HW_STREAM_BUFFER_INVALID && second != HW_STREAM_BUFFER_INVALID);
  CHECK(first != second);
  u8 bytes[64];
  memset(bytes, 0x5a, sizeof(bytes));
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
  completions = 0;
  hwFlushStream(bytes, 7, 11, second, transferDone, (uintptr_t)&completions);
  CHECK(completions == 1);
#else
  hwFlushStream(bytes, 7, 11, second);
#endif
  const u8* a = hwGetStreamPlayBuffer(first);
  const u8* b = hwGetStreamPlayBuffer(second);
  CHECK(a != NULL && b != NULL && a != b);
  for (u32 i = 0; i < 64; ++i) {
    CHECK(a[i] == 0);
    CHECK(b[i] == (i >= 7 && i < 18 ? 0x5a : 0));
  }
  hwExitStream(first);
  hwExitStream(second);
  CHECK(hwGetStreamPlayBuffer(first) == NULL && hwGetStreamPlayBuffer(second) == NULL);
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
static void testFilter(u32 rate) {
  u16 a, b;
  hwLowPassFrqToCoef(HW_LPF_FRQ_ARGS(0, 1000), &a, &b);
  double c = 2.0 - cos(2.0 * 3.14159265358979323846 * 1000 / rate);
  double feedback = c - sqrt(c * c - 1.0);
  CHECK(abs((int)a - (int)(32768 * (1.0 - feedback))) <= 1);
  CHECK(abs((int)b - (int)(32768 * feedback)) <= 1);
  hwSetFilter(0, 1, a, b);
  CHECK(dspVoice[0].filter.on && dspVoice[0].filter.coefA == a);
  hwSetFilter(0, 0, 0, 0);
  CHECK(!dspVoice[0].filter.on);
}
#endif

#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
static u8 virtualRing[256];
static unsigned virtualStarts, virtualStops;
static u32 virtualCallback(u8 reason, const SND_VIRTUALSAMPLE_INFO* info) {
  if (reason == SND_VIRTUALSAMPLE_REASON_INIT) {
    ++virtualStarts;
    sndVirtualSampleARAMUpdate(info->instID, virtualRing, 0, vs.bufferLength, 0, 0);
  } else if (reason == SND_VIRTUALSAMPLE_REASON_STOP) {
    ++virtualStops;
  }
  return 0;
}
static u32 voiceMessage(u32 reason, u32 voice) {
  if (reason == HW_MESSAGE_VIRTUAL_SAMPLE_START)
    return vsSampleStartNotify(voice);
  if (reason == HW_MESSAGE_VIRTUAL_SAMPLE_END)
    vsSampleEndNotify(dspVoice[voice].virtualSampleID);
  return 0;
}

static void testVirtualSamples(bool pcm16) {
  /* A regular stream occupies slot zero before virtual buffers are allocated. */
  u8 stream = hwInitStream(64);
  CHECK(stream != HW_STREAM_BUFFER_INVALID);
  CHECK(sndVirtualSampleAllocateBuffers(1, 64, pcm16 ? 1 : 0));
  CHECK(vs.streamBuffer[0].hwId != stream);
  size_t bytes;
  CHECK(aramGetStreamBufferAddress(vs.streamBuffer[0].hwId, &bytes) != 0);
  CHECK(bytes == (pcm16 ? vs.bufferLength * 2 : vs.bufferLength / 14 * 8));
  CHECK(bytes <= sizeof(virtualRing));
  memset(virtualRing, 0x11, sizeof(virtualRing));
  virtualStarts = virtualStops = 0;
  sndVirtualSampleSetCallback(virtualCallback);
  hwSetMesgCallback(voiceMessage);
  s16 prefix[16] = {1000, 1000, 1000, 1000};
  SAMPLE_INFO sample = {0};
  sample.addr = prefix;
  sample.length = sample.loopLength = pcm16 ? 4 : 14;
  sample.compType = pcm16 ? SAMPLE_TYPE_PCM16_VIRTUAL : SAMPLE_TYPE_ADPCM_VIRTUAL;
  hwInitSamplePlayback(0, 7, &sample, 1, 127, 0, 1, 1 HW_INIT_SAMPLE_UNUSED_ARG);
  hwSetSRCType(0, SAL_SRC_NONE);
  hwSetVolume(0, 0, 1.f, 64 << 16, 0, 0, 0);
  hwStart(0, 0);
  s16 output[1280];
  CHECK(sndPCRender(output, 640));
  CHECK(virtualStarts == 1);
  CHECK(dspVoice[0].vSampleInfo.inLoopBuffer);
  CHECK(dspVoice[0].vSampleInfo.loopBufferAddr == hwGetStreamPlayBuffer(vs.streamBuffer[0].hwId));
  bool audible = false;
  for (u32 i = 0; i < 1280; ++i)
    audible |= output[i] != 0;
  CHECK(audible);
  hwOff(0);
  CHECK(virtualStops == 1);
  sndVirtualSampleFreeBuffers();
  hwExitStream(stream);
}
#endif

int main(void) {
  SND_HOOKS hooks = {allocate, release};
  sndSetHooks(&hooks);
  testDirectoryConversion();
  testRawDirectory();
  for (u32 pass = 0; pass < 2; ++pass) {
    SND_PC_CONFIG config = {pass ? 48000 : 32000, 2};
    CHECK(sndPCConfigure(&config));
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 1)
    CHECK(sndInit(8, 4, 4, 2, 0, 0) == 0);
#else
    CHECK(sndInit(8, 4, 4, 0, 0) == 0);
#endif
    testSamples(SAMPLE_TYPE_PCM16, false);
    testGroup();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
    testSamples(SAMPLE_TYPE_PCM16, true);
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    testSamples(SAMPLE_TYPE_PCM16_VIRTUAL, true);
#endif
    testStreams();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
    testFilter(config.mixRate);
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
    testVirtualSamples(false);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    testVirtualSamples(true);
#endif
#endif
    sndQuit();
    CHECK(allocations == 0);
  }
  return 0;
}
