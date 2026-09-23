#include "hw_pc_assets.h"
#include "musyx/sal.h"
#include "musyx/seq.h"

#include <stdlib.h>
#include <string.h>

typedef struct PCField {
  size_t offset;
  u32 value;
  u8 width;
} PCField;

typedef struct PCReader {
  SND_PC_SPAN span;
  const char *section;
  SND_PC_ASSET_ERROR *error;
  PCField *fields;
  size_t count, capacity, extent;
  bool failed;
} PCReader;

static bool fail(PCReader *r, size_t offset, const char *reason) {
  if (!r->failed && r->error) {
    r->error->section = r->section;
    r->error->offset = offset;
    r->error->reason = reason;
  }
  r->failed = true;
  return false;
}

static const u8 *bytes(PCReader *r, size_t offset, size_t count) {
  if (r->failed)
    return NULL;
  /* Assets use 32-bit offsets. Bound both arithmetic and actual input extent
   * before constructing a pointer. Unknown-length legacy calls cannot prove
   * the latter and must only receive valid assets. */
  if (!r->span.data || offset > UINT32_MAX || count > UINT32_MAX - offset ||
      offset > r->span.size || count > r->span.size - offset) {
    fail(r, offset, "truncated or overflowing extent");
    return NULL;
  }
  if (offset + count > r->extent)
    r->extent = offset + count;
  return (const u8 *)r->span.data + offset;
}

static bool field(PCReader *r, size_t offset, u8 width, u32 value) {
  if (r->failed)
    return false;
  if (r->count == r->capacity) {
    size_t capacity = r->capacity ? r->capacity * 2 : 128;
    if (capacity > SIZE_MAX / sizeof(PCField))
      return fail(r, offset, "field table too large");
    PCField *storage = salMalloc(capacity * sizeof(*storage));
    if (!storage)
      return fail(r, offset, "out of memory");
    if (r->count)
      memcpy(storage, r->fields, r->count * sizeof(*storage));
    if (r->fields)
      salFree(r->fields);
    r->fields = storage;
    r->capacity = capacity;
  }
  r->fields[r->count++] = (PCField){offset, value, width};
  return true;
}

static u16 word(PCReader *r, size_t offset) {
  const u8 *p = bytes(r, offset, 2);
  u16 value = p ? salPCReadBE16(p) : 0;
  field(r, offset, 2, value);
  return value;
}

static u32 dword(PCReader *r, size_t offset) {
  const u8 *p = bytes(r, offset, 4);
  u32 value = p ? salPCReadBE32(p) : 0;
  field(r, offset, 4, value);
  return value;
}

static bool raw(PCReader *r, size_t offset, size_t length) {
  const u8 *p = bytes(r, offset, length);
  if (!p)
    return false;
  /* Recording byte payloads also detects aliases interpreted as incompatible
   * numeric fields (e.g. a curve aliased to big-endian macro words). */
  for (size_t i = 0; i < length; ++i)
    if (!field(r, offset + i, 1, p[i]))
      return false;
  return true;
}

static int compareField(const void *a, const void *b) {
  const PCField *lhs = a;
  const PCField *rhs = b;
  if (lhs->offset != rhs->offset)
    return lhs->offset < rhs->offset ? -1 : 1;
  return (int)lhs->width - rhs->width;
}

static void *materialize(PCReader *r) {
  if (r->failed)
    return NULL;
  qsort(r->fields, r->count, sizeof(*r->fields), compareField);
  for (size_t i = 1; i < r->count; ++i) {
    const PCField *a = &r->fields[i - 1];
    const PCField *b = &r->fields[i];
    if (b->offset < a->offset + a->width &&
        (a->offset != b->offset || a->width != b->width || a->value != b->value)) {
      fail(r, b->offset, "conflicting field interpretations");
      return NULL;
    }
  }
  u8 *output = salMalloc(r->extent ? r->extent : 1);
  if (!output) {
    fail(r, 0, "out of memory");
    return NULL;
  }
  memcpy(output, r->span.data, r->extent);
  for (size_t i = 0; i < r->count; ++i) {
    const PCField *f = &r->fields[i];
    if (f->width == 4) {
      memcpy(output + f->offset, &f->value, 4);
    } else if (f->width == 2) {
      u16 value = (u16)f->value;
      memcpy(output + f->offset, &value, 2);
    }
  }
  return output;
}

static bool idList(PCReader *r, size_t offset) {
  for (u32 count = 0; count <= 65536 && !r->failed; ++count) {
    u16 id = word(r, offset);
    if (id == 0xffff)
      return true;
    offset += 2;
    if (id & 0x8000) {
      u16 last = word(r, offset);
      if (last >= 0x8000 || last < (id & 0x3fff))
        return fail(r, offset, "invalid ID range");
      offset += 2;
    }
  }
  return fail(r, offset, "unterminated ID list");
}

static bool pages(PCReader *r, size_t offset) {
  for (u32 count = 0; count <= 128 && !r->failed; ++count, offset += 6) {
    const u8 *p = bytes(r, offset, 5);
    if (!p)
      return false;
    word(r, offset);
    raw(r, offset + 2, 3);
    if (p[4] == 0xff)
      return !r->failed;
    if (p[4] >= 128)
      return fail(r, offset + 4, "invalid program index");
    raw(r, offset + 5, 1);
  }
  return fail(r, offset, "unterminated program pages");
}

static bool project(PCReader *r) {
  size_t offset = 0;
  for (u32 group = 0; group <= 65536 && !r->failed; ++group) {
    u32 next = dword(r, offset);
    if (next == UINT32_MAX)
      return true;
    word(r, offset + 4);
    u16 type = word(r, offset + 6);
    if (type > 1)
      return fail(r, offset + 6, "unsupported group type");
    if (next < offset + (type ? 32 : 40))
      return fail(r, offset, "cyclic or overlapping group chain");
    for (u32 member = 8; member <= 24; member += 4) {
      u32 list = dword(r, offset + member);
      if (!list || !idList(r, list))
        return fail(r, offset + member, "missing group reference list");
    }
    u32 table = dword(r, offset + 28);
    if (type == 1) {
      u16 count = word(r, table);
      word(r, table + 2);
      if (!bytes(r, table + 4, (size_t)count * 10))
        return false;
      u16 previous = 0;
      for (u32 i = 0; i < count; ++i) {
        size_t entry = table + 4 + i * 10;
        u16 id = word(r, entry);
        if (i && id <= previous)
          return fail(r, entry, "FX IDs must be unique and sorted");
        previous = id;
        word(r, entry + 2);
        raw(r, entry + 4, 6);
      }
    } else {
      if (!pages(r, table) || !pages(r, dword(r, offset + 32)))
        return false;
      size_t setup = dword(r, offset + 36);
      if (setup > next)
        return fail(r, setup, "MIDI setups exceed group extent");
      u32 count;
      /* Some GC exporters delimit setups by group end, without a sentinel.
       * A terminal-only setup table is also accepted by the original API. */
      for (count = 0; setup < next && count <= 65536 && !r->failed; ++count, setup += 84) {
        if (word(r, setup) == 0xffff)
          break;
        if (84 > next - setup)
          return fail(r, setup, "partial MIDI setup at group end");
        word(r, setup + 2);
        raw(r, setup + 4, 80);
      }
      if (count > 65536)
        return fail(r, setup, "unterminated MIDI setups");
    }
    offset = next;
  }
  return fail(r, offset, "unterminated project");
}

static bool pool(PCReader *r) {
  u32 starts[4];
  for (u32 i = 0; i < 4; ++i)
    starts[i] = dword(r, i * 4);
  for (u32 kind = 0; kind < 4 && !r->failed; ++kind) {
    size_t offset = starts[kind];
    if (!offset)
      continue;
    if (offset < 16)
      return fail(r, offset, "pool section overlaps header");
    size_t limit = r->span.size;
    for (u32 i = 0; i < 4; ++i)
      if (starts[i] > offset && starts[i] < limit)
        limit = starts[i];
    for (u32 count = 0; count <= 65536 && !r->failed; ++count) {
      if (offset > limit || 2 > limit - offset)
        return fail(r, offset, "unterminated pool section");
      /* Some exporters wrote the curve-list sentinel as 0xFFFF. Accept both
       * a two-byte sentinel and 0x0000FFFF in its four-byte field. Never read
       * past a short sentinel into the next section or a macro instruction. */
      const u8 *prefix = bytes(r, offset, 2);
      if (!prefix)
        return false;
      if (kind == 1 && salPCReadBE16(prefix) == 0xffff) {
        word(r, offset);
        break;
      }
      if (4 > limit - offset)
        return fail(r, offset, "unterminated pool section");
      u32 next = dword(r, offset);
      if (next == UINT32_MAX || (kind == 1 && next == 0xffff))
        break;
      if (next < 8 || next > limit - offset || !bytes(r, offset, next))
        return fail(r, offset, "invalid relative pool entry extent");
      u16 id = word(r, offset + 4);
      word(r, offset + 6);
      if (kind == 0 && id >= 0x8000)
        return fail(r, offset + 4, "macro ID exceeds registry range");
      size_t payload = offset + 8;
      size_t length = next - 8;
      switch (kind) {
      case 0:
        if (length % 8)
          return fail(r, payload, "macro is not a sequence of word pairs");
        for (size_t i = 0; i < length; i += 4)
          dword(r, payload + i);
        break;
      case 1:
        /* Curves and little-endian ADSR payloads remain byte-oriented. */
        raw(r, payload, length);
        break;
      case 2:
        if (length < 128 * 8)
          return fail(r, payload, "short keymap");
        for (u32 i = 0; i < 128; ++i) {
          size_t item = payload + i * 8;
          word(r, item);
          raw(r, item + 2, 2);
          word(r, item + 4);
          raw(r, item + 6, 2);
        }
        break;
      case 3: {
        if (length < 4)
          return fail(r, payload, "short layer count");
        u32 layers = dword(r, payload);
        if (layers > 65535 || layers > (length - 4) / 12)
          return fail(r, payload, "invalid layer count");
        for (u32 i = 0; i < layers; ++i) {
          size_t item = payload + 4 + i * 12;
          word(r, item);
          raw(r, item + 2, 4);
          word(r, item + 6);
          raw(r, item + 8, 4);
        }
        break;
      }
      default:
        break;
      }
      offset += next;
      if (count == 65536)
        return fail(r, offset, "too many pool records");
    }
  }
  return !r->failed;
}

/* Repack validated pool chains into aligned native records. This keeps the
 * short curve sentinel from overlapping a following field and gives every
 * runtime MEM_DATA/MSTEP its natural alignment, regardless of exporter padding. */
static u32 poolRecordLength(const u8 *source, size_t offset, u32 kind) {
  if (kind == 1 && salPCReadBE16(source + offset) == 0xffff)
    return 0;
  u32 next = salPCReadBE32(source + offset);
  return next == UINT32_MAX || (kind == 1 && next == 0xffff) ? 0 : next;
}

static void *materializePool(PCReader *r, size_t *size) {
  const u8 *source = r->span.data;
  size_t total = 16;
  for (u32 kind = 0; kind < 4; ++kind) {
    size_t offset = salPCReadBE32(source + kind * 4);
    if (!offset)
      continue;
    u32 length;
    while ((length = poolRecordLength(source, offset, kind))) {
      total += ((size_t)length + 3) & ~(size_t)3;
      offset += length;
    }
    total += 4;
  }
  if (total > UINT32_MAX) {
    fail(r, 0, "native pool exceeds offset range");
    return NULL;
  }
  u8 *decoded = materialize(r);
  if (!decoded)
    return NULL;
  u8 *native = salMalloc(total);
  if (!native) {
    salFree(decoded);
    fail(r, 0, "out of memory");
    return NULL;
  }
  memset(native, 0, total);
  u32 dest = 16;
  for (u32 kind = 0; kind < 4; ++kind) {
    size_t offset = salPCReadBE32(source + kind * 4);
    if (!offset)
      continue;
    memcpy(native + kind * 4, &dest, 4);
    u32 length;
    while ((length = poolRecordLength(source, offset, kind))) {
      u32 stride = (length + 3u) & ~3u;
      memcpy(native + dest, decoded + offset, length);
      memcpy(native + dest, &stride, 4);
      if (kind == 1) {
        /* Private curve headers retain exact payload bounds despite padding. */
        u16 padding = stride - length;
        memcpy(native + dest + 6, &padding, 2);
      }
      dest += stride;
      offset += length;
    }
    const u32 terminal = UINT32_MAX;
    memcpy(native + dest, &terminal, 4);
    dest += 4;
  }
  salFree(decoded);
  *size = total;
  return native;
}

static bool directory(PCReader *r, SND_PC_SPAN samples, MusyPCGroupData *result) {
  size_t count = 0, extraBytes = 0;
  u16 previous = 0;
  for (; count < 65536 && !r->failed; ++count) {
    size_t offset = count * 32;
    u16 id = word(r, offset);
    if (id == 0xffff)
      break;
    if (count && id <= previous)
      return fail(r, offset, "sample IDs must be unique and sorted");
    previous = id;
    if (!bytes(r, offset, 32))
      return false;
    u32 dataOffset = dword(r, offset + 4);
    u32 encodedLength = dword(r, offset + 16);
    u32 length = encodedLength & 0xffffff;
    u32 type = encodedLength >> 24;
    u32 loop = dword(r, offset + 20);
    u32 loopLength = dword(r, offset + 24);
    if (loop > length || loopLength > length - loop)
      return fail(r, offset + 20, "invalid sample loop");
    if (type > SAMPLE_TYPE_ADPCM_VIRTUAL)
      return fail(r, offset + 16, "unsupported GC sample encoding");
    size_t payloadBytes = type == SAMPLE_TYPE_PCM16  ? (size_t)length * 2
                          : type == SAMPLE_TYPE_PCM8 ? length
                                                     : ((length + 13) / SND_STREAM_ADPCM_BLKSIZE) *
                                                           SND_STREAM_ADPCM_BLKBYTES;
    if (dataOffset > samples.size || payloadBytes > samples.size - dataOffset ||
        (!samples.data && payloadBytes && samples.size != SIZE_MAX))
      return fail(r, offset + 4, "sample payload exceeds sample section");
    u32 extra = dword(r, offset + 28);
    if (type != SAMPLE_TYPE_PCM16 && type != SAMPLE_TYPE_PCM8) {
      size_t extraLength = 40 + (type == SAMPLE_TYPE_ADPCM_PLUS
                                     ? (size_t)((length + 13) / SND_STREAM_ADPCM_BLKSIZE) * 6
                                     : 0);
      if (!extra || !bytes(r, extra, extraLength))
        return fail(r, offset + 28, "missing or truncated ADPCM metadata");
      extraBytes += extraLength;
    }
  }
  if (r->failed)
    return false;
  if (count == 65536)
    return fail(r, count * 32, "unterminated sample directory");
  size_t nativeSize = (count + 1) * sizeof(SDIR_DATA) + extraBytes;
  if (nativeSize > UINT32_MAX)
    return fail(r, 0, "native directory too large");
  SDIR_DATA *native = salMalloc(nativeSize);
  if (!native)
    return fail(r, 0, "out of memory");
  memset(native, 0, nativeSize);
  result->directory = native;
  result->directoryBytes = nativeSize;
  result->sampleCount = count;
  size_t extraOffset = (count + 1) * sizeof(SDIR_DATA);
  for (size_t i = 0; i < count; ++i) {
    const u8 *entry = (const u8 *)r->span.data + i * 32;
    SDIR_DATA *dest = &native[i];
    dest->id = salPCReadBE16(entry);
    dest->offset = salPCReadBE32(entry + 4);
    dest->header.info = salPCReadBE32(entry + 12);
    dest->header.length = salPCReadBE32(entry + 16);
    dest->header.loopOffset = salPCReadBE32(entry + 20);
    dest->header.loopLength = salPCReadBE32(entry + 24);
    u32 type = dest->header.length >> 24;
    if (type == SAMPLE_TYPE_PCM16 || type == SAMPLE_TYPE_PCM8)
      continue;
    u32 extra = salPCReadBE32(entry + 28);
    if (extra < count * 32 + 2)
      return fail(r, extra, "ADPCM metadata overlaps directory entries");
    const u8 *source = (const u8 *)r->span.data + extra;
    DSPADPCMplusInfo *metadata = (DSPADPCMplusInfo *)((u8 *)native + extraOffset);
    dest->extraData = (u32)extraOffset;
    metadata->numCoef = salPCReadBE16(source);
    metadata->initialPS = source[2];
    metadata->loopPS = source[3];
    metadata->loopY0 = (s16)salPCReadBE16(source + 4);
    metadata->loopY1 = (s16)salPCReadBE16(source + 6);
    for (u32 coefficient = 0; coefficient < 16; ++coefficient)
      metadata->coefTab[coefficient / 2][coefficient % 2] =
          (s16)salPCReadBE16(source + 8 + coefficient * 2);
    size_t blocks = type == SAMPLE_TYPE_ADPCM_PLUS
                        ? ((dest->header.length & 0xffffff) + 13) / SND_STREAM_ADPCM_BLKSIZE
                        : 0;
    for (size_t block = 0; block < blocks; ++block) {
      metadata->blk[block].Y0 = (s16)salPCReadBE16(source + 40 + block * 6);
      metadata->blk[block].Y1 = (s16)salPCReadBE16(source + 42 + block * 6);
      metadata->blk[block].PS = source[44 + block * 6];
      metadata->blk[block].reserved = source[45 + block * 6];
    }
    extraOffset += 40 + blocks * 6;
  }
  native[count].id = 0xffff;
  return true;
}

static bool arrangementStream(PCReader *r, size_t offset) {
  if (!offset)
    return true;
  const size_t begin = offset;
  for (u32 count = 0; count < 1000000; ++count) {
    const u8 *p = bytes(r, offset, 2);
    if (!p)
      return false;
    if (p[0] == 0x80 && p[1] == 0)
      return raw(r, begin, offset + 2 - begin);
    offset += p[0] & 0x80 ? 2 : 1;
    p = bytes(r, offset, 2);
    if (!p)
      return false;
    offset += p[0] & 0x80 ? 2 : 1;
  }
  return fail(r, offset, "unterminated controller stream");
}

static bool arrangementPattern(PCReader *r, size_t offset) {
  if (offset & 3)
    return fail(r, offset, "unaligned pattern header");
  dword(r, offset); /* Exporter headerLen is informational (normally 8). */
  u32 pitch = dword(r, offset + 4), modulation = dword(r, offset + 8);
  if (!arrangementStream(r, pitch) || !arrangementStream(r, modulation))
    return false;
  offset += 12;
  for (u32 count = 0; count < 1000000 && !r->failed; ++count) {
    word(r, offset);
    const u8 *event = bytes(r, offset + 2, 2);
    if (!event || !raw(r, offset + 2, 2))
      return false;
    if (event[0] == 0xff && event[1] == 0xff)
      return true;
    size_t length = (event[0] & 0x80) || !(event[0] | event[1]) ? 4 : 6;
    if (length == 6)
      word(r, offset + 4);
    offset += length;
  }
  return fail(r, offset, "unterminated pattern notes");
}

static bool arrangement(PCReader *r) {
  u32 trackTable = dword(r, 0), patternTable = dword(r, 4);
  u32 midiTable = dword(r, 8), master = dword(r, 12), info = dword(r, 16);
  if ((trackTable & 3) || (patternTable & 3) || (master & 3))
    return fail(r, 0, "unaligned arrangement table");
  if (trackTable < 24 || midiTable < 24 || !(info & 0x0fffffff))
    return fail(r, 0, "invalid arrangement header");
  u32 sections = 0;
  if (info & 0x80000000) {
    for (u32 i = 0; i < 16; ++i)
      dword(r, 20 + i * 4);
    sections = dword(r, 84);
    if (!raw(r, sections, 64))
      return false;
    for (u32 i = 0; i < 64; ++i)
      if (((const u8 *)r->span.data)[sections + i] >= 16)
        return fail(r, sections + i, "invalid track section");
  } else {
    dword(r, 20); /* Single-section exports have a 24-byte header. */
  }
  if (!raw(r, midiTable, 64))
    return false;
  u8 visited[8192] = {0};
  for (u32 track = 0; track < 64 && !r->failed; ++track) {
    size_t offset = dword(r, (size_t)trackTable + track * 4);
    if (!offset)
      continue;
    if (offset & 3)
      return fail(r, offset, "unaligned track");
    if (((const u8 *)r->span.data)[midiTable + track] >= 16)
      return fail(r, midiTable + track, "invalid MIDI channel");
    u32 count;
    for (count = 0; count <= 65536 && !r->failed; ++count, offset += 12) {
      dword(r, offset);
      raw(r, offset + 4, 4);
      u16 pattern = word(r, offset + 8);
      if (pattern == 0xfffe) {
        u16 target = word(r, offset + 10);
        if (target >= count)
          return fail(r, offset + 10, "track jump must refer to an earlier entry");
        break;
      }
      raw(r, offset + 10, 2);
      if (pattern == 0xffff)
        break;
      if (!(visited[pattern / 8] & (1u << (pattern % 8)))) {
        visited[pattern / 8] |= 1u << (pattern % 8);
        if (patternTable < 24)
          return fail(r, 4, "missing pattern table");
        size_t patternOffset = dword(r, (size_t)patternTable + pattern * 4);
        if (patternOffset < 24)
          return fail(r, patternOffset, "missing pattern data");
        if (!arrangementPattern(r, patternOffset))
          return false;
      }
    }
    if (count > 65536)
      return fail(r, offset, "unterminated track");
  }
  if (master) {
    u32 count;
    size_t offset = master;
    for (count = 0; count < 1000000 && !r->failed; ++count, offset += 8) {
      if (dword(r, offset) == UINT32_MAX)
        break;
      if (!dword(r, offset + 4))
        return fail(r, offset + 4, "zero tempo");
    }
    if (count == 1000000)
      return fail(r, offset, "unterminated tempo track");
  }
  return !r->failed;
}

bool salPCDecodeArrangement(SND_PC_SPAN input, void **result, size_t *size,
                            SND_PC_ASSET_ERROR *error) {
  *result = NULL;
  if (error)
    memset(error, 0, sizeof(*error));
  PCReader reader = {.span = input, .section = "arrangement", .error = error};
  bool ok = arrangement(&reader);
  if (ok) {
    *result = materialize(&reader);
    ok = *result != NULL;
    if (ok && size)
      *size = reader.extent;
  }
  if (reader.fields)
    salFree(reader.fields);
  return ok;
}

bool sndPCValidateArrangement(SND_PC_SPAN input, SND_PC_ASSET_ERROR *error) {
  void *result;
  if (!salPCDecodeArrangement(input, &result, NULL, error))
    return false;
  salFree(result);
  return true;
}

void salPCFreeGroupData(MusyPCGroupData *data) {
  if (data->project)
    salFree(data->project);
  if (data->pool)
    salFree(data->pool);
  if (data->directory)
    salFree(data->directory);
  memset(data, 0, sizeof(*data));
}

bool salPCDecodeGroup(const SND_PC_GROUP_ASSETS *assets, MusyPCGroupData *result,
                      SND_PC_ASSET_ERROR *error) {
  memset(result, 0, sizeof(*result));
  if (error)
    memset(error, 0, sizeof(*error));
  if (!assets) {
    if (error)
      *error = (SND_PC_ASSET_ERROR){"group", 0, "NULL assets"};
    return false;
  }
  PCReader readers[3] = {{.span = assets->project, .section = "project", .error = error},
                         {.span = assets->pool, .section = "pool", .error = error},
                         {.span = assets->directory, .section = "directory", .error = error}};
  bool ok = project(&readers[0]);
  if (ok && assets->pool.data)
    ok = pool(&readers[1]);
  if (ok)
    ok = directory(&readers[2], assets->samples, result);
  if (ok) {
    result->project = materialize(&readers[0]);
    result->pool = assets->pool.data ? materializePool(&readers[1], &result->poolBytes) : NULL;
    result->projectBytes = readers[0].extent;
    ok = result->project && (!assets->pool.data || result->pool);
  }
  for (u32 i = 0; i < 3; ++i)
    if (readers[i].fields)
      salFree(readers[i].fields);
  if (!ok)
    salPCFreeGroupData(result);
  return ok;
}

bool sndPCValidateGroup(const SND_PC_GROUP_ASSETS *assets, SND_PC_ASSET_ERROR *error) {
  MusyPCGroupData decoded;
  bool ok = salPCDecodeGroup(assets, &decoded, error);
  if (ok)
    salPCFreeGroupData(&decoded);
  return ok;
}
