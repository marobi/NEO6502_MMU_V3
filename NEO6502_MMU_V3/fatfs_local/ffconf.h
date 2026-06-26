/* ---------------------------------------------------------------------------/
/  NEO6502_MMU local FatFs configuration
/----------------------------------------------------------------------------/
/  Based on Arduino-Pico 5.6.1 libraries/FatFS/src/ffconf.h.
/  This file is local to the project and is used by fatfs_local/ff.cpp.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF 80286

#define FF_FS_READONLY 0
#define FF_FS_MINIMIZE 0
#define FF_USE_FIND 1
#define FF_USE_MKFS 1
#define FF_USE_FASTSEEK 1
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 1
#define FF_USE_LABEL 1
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI 1
#define FF_PRINT_FLOAT 1
#define FF_STRF_ENCODE 3

#define FF_CODE_PAGE 437
#define FF_USE_LFN 1
#define FF_MAX_LFN 64
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF 64
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0

#define FF_VOLUMES 4
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS "USB0","USB1","USB2","USB3"
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS 512
#define FF_MAX_SS 4096
#define FF_LBA64 0
#define FF_MIN_GPT 0x10000000
#define FF_USE_TRIM 1

#define FF_FS_TINY 0
#define FF_FS_EXFAT 0
#define FF_FS_NORTC 0
#define FF_NORTC_MON 1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2026
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK 0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT 1000
#define FF_SYNC_t int
