/* Native sample registrations. Only referenced samples own uploaded PCM/ADPCM;
 * input payload pointers are borrowed for the duration of a group push. */
#include "hw_pc_assets.h"
#include "musyx/hardware.h"
#include <string.h>

static SDIR_TAB directories[128];
static u16 directoryCount;
static SDIR_TAB *uploading;

void salPCResetSampleData(void) {
  memset(directories, 0, sizeof(directories));
  directoryCount = 0;
  uploading = NULL;
}

void salPCFinishSampleUpload(void) {
  if (uploading)
    uploading->base = NULL;
  uploading = NULL;
}

static SDIR_DATA *findSample(SDIR_TAB *table, u16 id) {
  size_t lo = 0, hi = table->numSmp;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (table->data[mid].id < id)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo < table->numSmp && table->data[lo].id == id ? table->data + lo : NULL;
}

static SDIR_DATA *activeSample(u16 id, SDIR_TAB **owner) {
  for (u16 i = 0; i < directoryCount; ++i) {
    SDIR_DATA *entry = findSample(&directories[i], id);
    if (entry && entry->ref_cnt) {
      if (owner)
        *owner = &directories[i];
      return entry;
    }
  }
  return NULL;
}

bool dataInsertSDir(SDIR_DATA *sdir, void *samples) {
  if (!sdir || directoryCount == 128)
    return false;
  for (u16 i = 0; i < directoryCount; ++i) {
    if (directories[i].data == sdir) {
      uploading = &directories[i];
      uploading->base = samples;
      return true;
    }
  }
  size_t count = 0;
  while (sdir[count].id != 0xffff) {
    if (count == UINT16_MAX)
      return false;
    sdir[count].ref_cnt = 0;
    sdir[count].addr = NULL;
    ++count;
  }
  uploading = &directories[directoryCount++];
  *uploading = (SDIR_TAB){sdir, samples, (u16)count, 0};
  return true;
}

bool dataRemoveSDir(SDIR_DATA *sdir) {
  for (u16 i = 0; i < directoryCount; ++i) {
    if (directories[i].data != sdir)
      continue;
    for (u16 j = 0; j < directories[i].numSmp; ++j)
      if (sdir[j].ref_cnt)
        return false;
    salPCFinishSampleUpload();
    memmove(&directories[i], &directories[i + 1], (--directoryCount - i) * sizeof(directories[0]));
    return true;
  }
  return false;
}

bool dataAddSampleReference(u16 id) {
  SDIR_DATA *entry = activeSample(id, NULL);
  if (entry) {
    if (entry->ref_cnt == UINT16_MAX)
      return false;
    ++entry->ref_cnt;
    return true;
  }
  if (!uploading || !(entry = findSample(uploading, id)))
    return false;
  /* A NULL sample base denotes the SDK's file-offset upload callback path. */
  entry->addr = (void *)((uintptr_t)uploading->base + entry->offset);
  SAMPLE_HEADER *header = &entry->header;
  hwSaveSample(&header, &entry->addr);
  if (!entry->addr)
    return false;
  entry->ref_cnt = 1;
  return true;
}

bool dataRemoveSampleReference(u16 id) {
  SDIR_DATA *entry = activeSample(id, NULL);
  if (!entry)
    return false;
  if (--entry->ref_cnt == 0) {
    hwRemoveSample(&entry->header, entry->addr);
    entry->addr = NULL;
  }
  return true;
}

s32 dataGetSample(u16 id, SAMPLE_INFO *sample) {
  SDIR_TAB *owner;
  SDIR_DATA *entry = activeSample(id, &owner);
  if (!entry || !sample)
    return -1;
  memset(sample, 0, sizeof(*sample));
  sample->info = entry->header.info;
  sample->addr = entry->addr;
  sample->length = entry->header.length & 0xffffff;
  sample->compType = entry->header.length >> 24;
  sample->loop = entry->header.loopOffset;
  sample->loopLength = entry->header.loopLength;
  if (entry->extraData)
    sample->extraData = (u8 *)owner->data + entry->extraData;
  return 0;
}
