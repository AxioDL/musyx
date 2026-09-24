#ifndef _MUSYX_VERSION
#define _MUSYX_VERSION

#if _DEBUG
#define MUSY_BUILD_DEBUG
#endif

#define MUSY_TARGET_PC 0
#define MUSY_TARGET_DOLPHIN 1

#ifndef MUSY_TARGET
#define MUSY_TARGET MUSY_TARGET_PC
#endif

#ifndef MUSY_VERSION_CHECK
#define MUSY_VERSION_CHECK(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#endif

#ifndef MUSY_VERSION_MAJOR
#define MUSY_VERSION_MAJOR 2
#endif

#ifndef MUSY_VERSION_MINOR
#define MUSY_VERSION_MINOR 0
#endif

#ifndef MUSY_VERSION_PATCH
#define MUSY_VERSION_PATCH 0
#endif

#ifndef MUSY_VERSION
#define MUSY_VERSION MUSY_VERSION_CHECK(MUSY_VERSION_MAJOR, MUSY_VERSION_MINOR, MUSY_VERSION_PATCH)
#endif

// Game-specific builds that don't fit the linear version order
#define MUSY_BUILD_DEFAULT 0
// Metroid Prime 2: Echoes: 2.0.3 without the later stream/studio rework seen in Super Mario Strikers
#define MUSY_BUILD_MP2 1

#ifndef MUSY_BUILD
#define MUSY_BUILD MUSY_BUILD_DEFAULT
#endif

#endif // _MUSYX_VERSION
