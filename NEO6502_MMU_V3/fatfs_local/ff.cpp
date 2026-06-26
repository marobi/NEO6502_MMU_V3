// ============================================================
// fatfs_local/ff.cpp
// NEO6502_MMU local FatFs build shim
//
// This file makes ffconf.h local without patching Arduino-Pico core files.
// It includes the local namespace-compatible ff.h/diskio.h first. That defines
// FF_DEFINED and _DISKIO_DEFINED, so when the Arduino-Pico implementation is
// included below, its own ff.h/diskio.h are skipped by their include guards.
// ============================================================

#include "ff.h"
#include "diskio.h"

#if FF_VOLUMES < 4
#error "fatfs_local/ffconf.h must define FF_VOLUMES >= 4"
#endif

// Prefer the known Arduino-Pico 5.6.1 package location relative to Rien's
// Visual Studio project layout. Keep a fallback through the build include path
// for machines where the FatFS src directory is still on the include path.
#if __has_include("../../../../../AppData/Local/arduino15/packages/rp2040/hardware/rp2040/5.6.1/libraries/FatFS/src/ff.cpp")
  #include "../../../../../AppData/Local/arduino15/packages/rp2040/hardware/rp2040/5.6.1/libraries/FatFS/src/ff.cpp"
#elif __has_include(<ff.cpp>)
  #include <ff.cpp>
#else
  #error "Cannot locate Arduino-Pico 5.6.1 FatFS implementation ff.cpp. Keep libraries/FatFS/src on the include path or update fatfs_local/ff.cpp."
#endif
