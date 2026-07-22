// ============================================================
// fatfs_local/ffunicode.cpp
// NEO6502_MMU local FatFs Unicode/codepage helper build shim
//
// This file keeps FatFs helper functions in the same local configuration
// domain as fatfs_local/ff.cpp. It includes the local ff.h first so the
// Arduino-Pico implementation sees the same FF_VOLUMES and LFN settings.
// ============================================================

#include "ff.h"

#if FF_VOLUMES < 4
#error "fatfs_local/ffconf.h must define FF_VOLUMES >= 4"
#endif

#if __has_include("../../../../../AppData/Local/arduino15/packages/rp2040/hardware/rp2040/6.0.0/libraries/FatFS/src/ffunicode.cpp")
  #include "../../../../../AppData/Local/arduino15/packages/rp2040/hardware/rp2040/6.0.0/libraries/FatFS/src/ffunicode.cpp"
#elif __has_include(<ffunicode.cpp>)
  #include <ffunicode.cpp>
#else
  #error "Cannot locate Arduino-Pico 5.7.0 FatFS implementation ffunicode.cpp. Keep libraries/FatFS/src on the include path or update fatfs_local/ffunicode.cpp."
#endif
