#ifndef _MUSYX_ARAMDMA
#define _MUSYX_ARAMDMA

#include "musyx/musyx.h"
#include "musyx/hardware.h"

#ifdef __cplusplus
extern "C" {
#endif

extern u32 aramSize;
extern u8* aramBase;
#if MUSY_TARGET == MUSY_TARGET_DOLPHIN
void aramInit(u32 length);
void aramExit();
size_t aramGetStreamBufferAddress(u8 id, size_t* len);
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 3)
void aramUploadData(void* mram, u32 aram, u32 len, u32 highPrio, void (*callback)(size_t),
                    MUSY_HOST_USER user);
#else
void aramUploadData(void* mram, u32 aram, u32 len, u32 highPrio);
#endif
void aramFreeStreamBuffer(u8 id);
void* aramStoreData(void* src, u32 len
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
                    ,
                    ARAMInfo* ai
#endif
);
void aramRemoveData(void* aram, u32 len
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
                    ,
                    ARAMInfo* ai
#endif
);
u8 aramAllocateStreamBuffer(u32 len);
unsigned long aramGetZeroBuffer();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
u32 aramGetAvailableBytes(ARAMInfo* ai);
#endif
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
void aramSetUploadCallback(ARAMUploadCallback callback, u32 chunckSize);
#endif
void aramSyncTransferQueue();
#elif MUSY_TARGET == MUSY_TARGET_PC
void aramInit(unsigned long length);
void aramExit();
size_t aramGetStreamBufferAddress(u8 id, size_t* len);
void aramUploadData(void* mram, size_t aram, u32 len, u32 highPrio, void (*callback)(size_t),
                    MUSY_HOST_USER user);
void aramFreeStreamBuffer(u8 id);
void* aramStoreData(void* src, unsigned long len
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
                    ,
                    ARAMInfo* ai
#endif
);
void aramRemoveData(void* aram, unsigned long len
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 1)
                    ,
                    ARAMInfo* ai
#endif
);
u8 aramAllocateStreamBuffer(u32 len);
unsigned long aramGetZeroBuffer();
#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
u32 aramGetAvailableBytes(ARAMInfo* ai);
#endif
void aramSetUploadCallback(ARAMUploadCallback callback, unsigned long chunckSize);
void aramSyncTransferQueue();
#endif

#ifdef __cplusplus
}
#endif

#endif // _MUSYX_ARAMDMA
