#ifndef _MUSYX_S3D
#define _MUSYX_S3D

#include "musyx/musyx.h"

#include "musyx/synthdata.h"

#ifdef __cplusplus
extern "C" {
#endif

void s3dKillAllEmitter();
// 1.5 Patch 2 (note 40): flags carry SND_FLAGS_EMITTERGROUPSUSEMAXVOICES
#if MUSY_VERSION >= MUSY_VERSION_CHECK(1, 5, 2)
void s3dInit(u32); /* extern */
#else
void s3dInit(); /* extern */
#endif
void s3dKillEmitterByFXID(FX_TAB* fxTab, u32 num);
void s3dExit();
void s3dHandle();

#ifdef __cplusplus
}
#endif

#endif
