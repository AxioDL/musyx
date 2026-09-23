#ifndef MUSYX_PC_ASSETS_H
#define MUSYX_PC_ASSETS_H

#include "musyx/pc.h"
#include "musyx/synthdata.h"

static inline u16 salPCReadBE16(const void* ptr) {
  const u8* p = ptr;
  return (u16)((u16)p[0] << 8 | p[1]);
}
static inline u32 salPCReadBE32(const void* ptr) {
  const u8* p = ptr;
  return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static inline u16 salPCReadLE16(const void* ptr) {
  const u8* p = ptr;
  return (u16)((u16)p[1] << 8 | p[0]);
}
static inline u32 salPCReadLE32(const void* ptr) {
  const u8* p = ptr;
  return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
}

typedef struct MusyPCGroupData {
  void* project;
  void* pool;
  SDIR_DATA* directory;
  size_t projectBytes, poolBytes, directoryBytes;
  size_t sampleCount;
} MusyPCGroupData;

/* Shared by the bounded inspector and the legacy valid-input loading path.
 * SIZE_MAX denotes an unknown input extent, not validated memory safety. */
bool salPCDecodeGroup(const SND_PC_GROUP_ASSETS* assets, MusyPCGroupData* result,
                      SND_PC_ASSET_ERROR* error);
void salPCFreeGroupData(MusyPCGroupData* data);

bool salPCDecodeArrangement(SND_PC_SPAN input, void** result, size_t* size,
                            SND_PC_ASSET_ERROR* error);
void* salPCAcquireSong(const void* input);
void salPCReleaseSong(void* song);
void salPCCollectSongs(void);
void salPCResetSampleData(void);
void salPCFinishSampleUpload(void);
bool salPCRegistrationSpace(u32 macros, u32 curves, u32 keymaps, u32 layers);

#endif
