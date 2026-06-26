# Local FatFs for NEO6502_MMU

This version does not patch Arduino-Pico's installed FatFS library files.

`fatfs_local/ffconf.h` is the authoritative FatFs configuration for this project.
It sets:

```cpp
#define FF_VOLUMES 4
#define FF_VOLUME_STRS "USB0","USB1","USB2","USB3"
```

`fatfs_local/ff.cpp` is a build shim. It includes the local `ff.h`, `diskio.h`, and
`ffconf.h` first, then includes Arduino-Pico 5.6.1's FatFs implementation file.
Because the local headers define the same include guards used by Arduino-Pico's
headers, the implementation compiles against the local configuration.

Expected build evidence:

```text
...\NEO6502_MMU_V3\fatfs_local\ff.cpp
```

Expected runtime evidence with two FAT sticks:

```text
*I: USB FatFs: mounting drive 0:
*I: USB FatFs mounted: drive 0:
*I: USB FatFs: mounting drive 1:
*I: USB FatFs mounted: drive 1:
```

If the build also compiles Arduino-Pico's `libraries/FatFS/src/ff.cpp` as a separate
translation unit, that is wrong and will cause duplicate FatFs symbols. The project
should compile only `fatfs_local/ff.cpp` for FatFs.


## V28b link fix

`fatfs_local/ffunicode.cpp` is compiled locally together with `fatfs_local/ff.cpp`.
It provides the FatFs Unicode/codepage helper symbols used when long filename / UTF-8 support is enabled:

```text
fatfs::ff_wtoupper
fatfs::ff_oem2uni
fatfs::ff_uni2oem
```

These helpers must be built with the same local `ffconf.h` as `ff.cpp`; otherwise the local FatFs build links incompletely.
