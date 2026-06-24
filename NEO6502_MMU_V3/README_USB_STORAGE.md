# NEO6502_MMU RP2350 USB host storage status

Current storage stack:

```text
RP2350 native USB host
  -> Adafruit TinyUSB MSC host
  -> usb_storage_read_blocks()
  -> usb_msc_diskio.*
  -> Arduino-Pico FatFS
  -> rp_fs.* RP-side file handles
  -> later 6502 mailbox FS_STATUS / FS_OPEN / FS_READ / FS_CLOSE
```

## Current validated state

The manual FAT32/MBR parser has been removed from the runtime path. The system
now mounts the USB stick through FatFs and reads files through the RP-side handle
API.

Validated before this version:

```text
USB MSC mount                 OK
USB block geometry            OK
FatFs f_mount("0:")           OK
TEST.TXT small read           OK
BIG.TXT multi-cluster read    OK
```

This version moves the read validation from `usb_fatfs.*` into `rp_fs.*`.
`usb_fatfs.*` now only owns FatFs mount/unmount state.

Expected startup output:

```text
*I: USB MSC mounted: dev=1 lun=0
    block size        : 512
    block count       : ...
*I: USB FatFs: mounting drive 0:
*I: USB FatFs mounted
    text: This is a test
*I: RP FS: read TEST.TXT size=14 read=14 chunks=1 checksum=000004F5
*I: RP FS: read BIG.TXT size=49190 read=49190 chunks=193 checksum=003D828E
```

## RP-local API

`rp_fs.h` exposes:

```cpp
bool rp_fs_ready();
int rp_fs_open_readonly_83(const char* filename);
bool rp_fs_close(uint8_t handle);
int rp_fs_read(uint8_t handle, uint8_t* dst, uint16_t len);
uint32_t rp_fs_size(uint8_t handle);
uint32_t rp_fs_position(uint8_t handle);
void rp_fs_close_all();
```

Current constraints:

```text
read-only
4 RP-side file handles
FatFs drive 0:
no 6502 mailbox binding yet
no write/seek/sync mailbox commands yet
```

## Visual Micro / Arduino-Pico notes

Use the plain Arduino-Pico FatFS library:

```text
...\packages\rp2040\hardware\rp2040\5.6.1\libraries\FatFS\src
```

Do not use `FatFSUSB`. It is incompatible with the Adafruit TinyUSB host setup
used here.
