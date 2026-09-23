#include "musyx/hardware.h"
#if MUSY_TARGET == MUSY_TARGET_PC
#include <stdlib.h>
#endif

void *salMalloc(size_t len) {
#if MUSY_TARGET == MUSY_TARGET_PC
  if (!salHooks.malloc)
    return malloc(len);
#endif
  return salHooks.malloc(len);
}

#if MUSY_VERSION >= MUSY_VERSION_CHECK(2, 0, 2)
void *salMallocPhysical(size_t len) { return salHooks.mallocPhysical(len); }
#endif

void salFree(void *addr) {
#if MUSY_TARGET == MUSY_TARGET_PC
  if (!salHooks.free) {
    free(addr);
    return;
  }
#endif
  salHooks.free(addr);
}
