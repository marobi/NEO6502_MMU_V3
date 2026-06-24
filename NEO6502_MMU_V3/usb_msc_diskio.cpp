#include "usb_msc_diskio.h"
#include "usb_storage.h"

// ============================================================
// usb_msc_diskio.cpp
// NEO MMU - FatFs disk I/O implementation over TinyUSB MSC host
//
// Arduino-Pico 5.6.x places FatFs and diskio symbols in namespace fatfs.
// Keep these definitions in the same namespace or ff.c/ff.cpp will not link
// against this disk backend.
// ============================================================

namespace fatfs {

static bool valid_drive(BYTE pdrv) {
  return pdrv == USB_MSC_DISKIO_PDRV;
}

DSTATUS disk_initialize(BYTE pdrv) {
  if (!valid_drive(pdrv))
    return STA_NOINIT;

  if (!usb_storage_host_enabled())
    return STA_NOINIT;

  if (!usb_storage_device_mounted())
    return STA_NOINIT | STA_NODISK;

  if (usb_storage_block_size() != 512 || usb_storage_block_count() == 0)
    return STA_NOINIT;

#if USB_MSC_DISKIO_READONLY
  return STA_PROTECT;
#else
  return 0;
#endif
}

DSTATUS disk_status(BYTE pdrv) {
  if (!valid_drive(pdrv))
    return STA_NOINIT;

  if (!usb_storage_host_enabled())
    return STA_NOINIT;

  if (!usb_storage_device_mounted())
    return STA_NOINIT | STA_NODISK;

  if (usb_storage_block_size() != 512 || usb_storage_block_count() == 0)
    return STA_NOINIT;

#if USB_MSC_DISKIO_READONLY
  return STA_PROTECT;
#else
  return 0;
#endif
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  if (!valid_drive(pdrv) || buff == nullptr || count == 0)
    return RES_PARERR;

  if (!usb_storage_device_mounted())
    return RES_NOTRDY;

  if (usb_storage_block_size() != 512)
    return RES_ERROR;

  uint32_t const block_count = usb_storage_block_count();
  uint32_t const first = (uint32_t)sector;
  uint32_t const blocks = (uint32_t)count;

  if (first >= block_count || blocks > (block_count - first))
    return RES_PARERR;

  return usb_storage_read_blocks(first, buff, (uint16_t)blocks, 5000) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
#if USB_MSC_DISKIO_READONLY
  (void)pdrv;
  (void)buff;
  (void)sector;
  (void)count;
  return RES_WRPRT;
#else
  if (!valid_drive(pdrv) || buff == nullptr || count == 0)
    return RES_PARERR;

  if (!usb_storage_device_mounted())
    return RES_NOTRDY;

  if (usb_storage_block_size() != 512)
    return RES_ERROR;

  uint32_t const block_count = usb_storage_block_count();
  uint32_t const first = (uint32_t)sector;
  uint32_t const blocks = (uint32_t)count;

  if (first >= block_count || blocks > (block_count - first))
    return RES_PARERR;

  return usb_storage_write_blocks(first, buff, (uint16_t)blocks, 5000) ? RES_OK : RES_ERROR;
#endif
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  if (!valid_drive(pdrv))
    return RES_PARERR;

  if (!usb_storage_device_mounted())
    return RES_NOTRDY;

  switch (cmd) {
    case CTRL_SYNC:
      return usb_storage_sync() ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
      if (buff == nullptr)
        return RES_PARERR;
      *(DWORD*)buff = usb_storage_block_count();
      return RES_OK;

    case GET_SECTOR_SIZE:
      if (buff == nullptr)
        return RES_PARERR;
      *(WORD*)buff = (WORD)usb_storage_block_size();
      return RES_OK;

    case GET_BLOCK_SIZE:
      if (buff == nullptr)
        return RES_PARERR;
      *(DWORD*)buff = 1;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}

} // namespace fatfs
