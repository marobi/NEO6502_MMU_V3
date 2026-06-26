# NEO6502_MMU V28a true multi-stick FatFS requirement

V28a maps USB MSC slot N to FatFs logical drive `N:`:

```text
slot 0 -> 0:
slot 1 -> 1:
slot 2 -> 2:
slot 3 -> 3:
```

The Arduino-Pico 5.6.1 FatFS library ships with:

```cpp
#define FF_VOLUMES 1
```

With that default, `f_mount("1:")` fails with `FR_INVALID_DRIVE` (`fr=11`) before FatFs reads the USB stick. This is not a FAT32/exFAT formatting error.

Because FatFS `ff.cpp` includes `ffconf.h` from the Arduino-Pico package directory, a project-local `ffconf.h` is not enough. Patch the installed Arduino-Pico configuration once:

```text
tools\patch_arduino_pico_fatfs_4volumes.bat
```

This changes:

```cpp
#define FF_VOLUMES 4
#define FF_VOLUME_STRS "USB0","USB1","USB2","USB3"
```

Then do a full Visual Micro clean/rebuild.

`usb_fatfs.cpp` now contains a compile-time guard. If the Arduino-Pico package is still configured for one volume, the build stops instead of producing firmware that can only mount `0:`.

After every Arduino-Pico core update/reinstall, check this file again:

```text
%LOCALAPPDATA%\Arduino15\packages\rp2040\hardware\rp2040\5.6.1\libraries\FatFS\src\ffconf.h
```
