#ifndef _MUSYX_VSAMPLES
#define _MUSYX_VSAMPLES

#include "musyx/musyx.h"
#include "musyx/synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VS_MAX_BUFFERS 64
#define VS_BUFFER_NONE 0xFF
#define VS_VOICE_NONE 0xFF

#define VS_STATE_FREE 0
#define VS_STATE_STREAMING 1
#define VS_STATE_DRAINING 2       // Notify the macro when the final samples have played.
#define VS_STATE_DRAINING_ABORT 3 // Kill the voice after the final samples have played.

#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
typedef struct VS_BUFFER {
  // total size: 0x24
  u8 state;                    // offset 0x0, size 0x1
  u8 hwId;                     // offset 0x1, size 0x1
  u8 smpType;                  // offset 0x2, size 0x1
  u8 voice;                    // offset 0x3, size 0x1
  u32 last;                    // offset 0x4, size 0x4
  u32 finalGoodSamples;        // offset 0x8, size 0x4
  u32 finalLast;               // offset 0xC, size 0x4
  SND_VIRTUALSAMPLE_INFO info; // offset 0x10, size 0x14
} VS_BUFFER;

typedef struct _VS {
  // total size: 0x950
  u8 numBuffers;                          // offset 0x0, size 0x1
  u32 bufferLength;                       // offset 0x4, size 0x4
  VS_BUFFER streamBuffer[VS_MAX_BUFFERS]; // offset 0x8, size 0x900
  u8 voices[SYNTH_MAX_VOICES];            // offset 0x908, size 0x40
  u16 nextInstID;                         // offset 0x948, size 0x2
  u32 (*callback)(u8,
                  const SND_VIRTUALSAMPLE_INFO*); // offset 0x94C, size 0x4
} VS;

extern VS vs;
#endif

// Virtual samples, first in 1.5 Patch 3; they need the per-transfer ARAM callbacks (note 52)
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
void vsInit(); /* extern */
u32 vsSampleStartNotify(
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
    u32 voiceID
#else
    u8 voice
#endif
);
void vsSampleEndNotify(u32 pubID);
void vsSampleUpdates();
#endif

#ifdef __cplusplus
}
#endif

#endif // _MUSYX_VSAMPLES
