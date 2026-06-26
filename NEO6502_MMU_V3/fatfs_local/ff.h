/* ----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem module R0.15 interface
/  Local NEO6502_MMU project header, namespace-compatible with Arduino-Pico.
/----------------------------------------------------------------------------*/
#ifndef FF_DEFINED
#define FF_DEFINED 80286

#include "ffconf.h"

#if FF_DEFINED != FFCONF_DEF
#error Wrong configuration file (ffconf.h).
#endif

#include <stdint.h>
#include <stddef.h>

namespace fatfs {

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t QWORD;
typedef WORD WCHAR;

typedef DWORD FSIZE_t;
typedef DWORD LBA_t;

typedef char TCHAR;
#define _T(x) x
#define _TEXT(x) x

#if FF_MULTI_PARTITION
typedef struct { BYTE pd; BYTE pt; } PARTITION;
extern PARTITION VolToPart[];
#endif

#if FF_STR_VOLUME_ID
#ifndef FF_VOLUME_STRS
extern const char* VolumeStr[FF_VOLUMES];
#endif
#endif

typedef struct {
  BYTE fs_type;
  BYTE pdrv;
  BYTE ldrv;
  BYTE n_fats;
  BYTE wflag;
  BYTE fsi_flag;
  WORD id;
  WORD n_rootdir;
  WORD csize;
#if FF_MAX_SS != FF_MIN_SS
  WORD ssize;
#endif
#if FF_USE_LFN
  WCHAR* lfnbuf;
#endif
#if FF_FS_EXFAT
  BYTE* dirbuf;
#endif
#if !FF_FS_READONLY
  DWORD last_clst;
  DWORD free_clst;
#endif
#if FF_FS_RPATH
  DWORD cdir;
#if FF_FS_EXFAT
  DWORD cdc_scl;
  DWORD cdc_size;
  DWORD cdc_ofs;
#endif
#endif
  DWORD n_fatent;
  DWORD fsize;
  LBA_t volbase;
  LBA_t fatbase;
  LBA_t dirbase;
  LBA_t database;
#if FF_FS_EXFAT
  LBA_t bitbase;
#endif
  LBA_t winsect;
  BYTE win[FF_MAX_SS];
} FATFS;

typedef struct {
  FATFS* fs;
  WORD id;
  BYTE attr;
  BYTE stat;
  DWORD sclust;
  FSIZE_t objsize;
#if FF_FS_EXFAT
  DWORD n_cont;
  DWORD n_frag;
  DWORD c_scl;
  DWORD c_size;
  DWORD c_ofs;
#endif
#if FF_FS_LOCK
  UINT lockid;
#endif
} FFOBJID;

typedef struct {
  FFOBJID obj;
  BYTE flag;
  BYTE err;
  FSIZE_t fptr;
  DWORD clust;
  LBA_t sect;
#if !FF_FS_READONLY
  LBA_t dir_sect;
  BYTE* dir_ptr;
#endif
#if FF_USE_FASTSEEK
  DWORD* cltbl;
#endif
#if !FF_FS_TINY
  BYTE buf[FF_MAX_SS];
#endif
} FIL;

typedef struct {
  FFOBJID obj;
  DWORD dptr;
  DWORD clust;
  LBA_t sect;
  BYTE* dir;
  BYTE fn[12];
#if FF_USE_LFN
  DWORD blk_ofs;
#endif
#if FF_USE_FIND
  const TCHAR* pat;
#endif
} DIR;

typedef struct {
  FSIZE_t fsize;
  WORD fdate;
  WORD ftime;
  BYTE fattrib;
#if FF_USE_LFN
  TCHAR altname[FF_SFN_BUF + 1];
  TCHAR fname[FF_LFN_BUF + 1];
#else
  TCHAR fname[12 + 1];
#endif
} FILINFO;

typedef struct {
  BYTE fmt;
  BYTE n_fat;
  UINT align;
  UINT n_root;
  DWORD au_size;
} MKFS_PARM;

typedef enum {
  FR_OK = 0,
  FR_DISK_ERR,
  FR_INT_ERR,
  FR_NOT_READY,
  FR_NO_FILE,
  FR_NO_PATH,
  FR_INVALID_NAME,
  FR_DENIED,
  FR_EXIST,
  FR_INVALID_OBJECT,
  FR_WRITE_PROTECTED,
  FR_INVALID_DRIVE,
  FR_NOT_ENABLED,
  FR_NO_FILESYSTEM,
  FR_MKFS_ABORTED,
  FR_TIMEOUT,
  FR_LOCKED,
  FR_NOT_ENOUGH_CORE,
  FR_TOO_MANY_OPEN_FILES,
  FR_INVALID_PARAMETER
} FRESULT;

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, FSIZE_t ofs);
FRESULT f_truncate(FIL* fp);
FRESULT f_sync(FIL* fp);
FRESULT f_opendir(DIR* dp, const TCHAR* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_findfirst(DIR* dp, FILINFO* fno, const TCHAR* path, const TCHAR* pattern);
FRESULT f_findnext(DIR* dp, FILINFO* fno);
FRESULT f_mkdir(const TCHAR* path);
FRESULT f_unlink(const TCHAR* path);
FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new);
FRESULT f_stat(const TCHAR* path, FILINFO* fno);
FRESULT f_chmod(const TCHAR* path, BYTE attr, BYTE mask);
FRESULT f_utime(const TCHAR* path, const FILINFO* fno);
FRESULT f_chdir(const TCHAR* path);
FRESULT f_chdrive(const TCHAR* path);
FRESULT f_getcwd(TCHAR* buff, UINT len);
FRESULT f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs);
FRESULT f_getlabel(const TCHAR* path, TCHAR* label, DWORD* vsn);
FRESULT f_setlabel(const TCHAR* label);
FRESULT f_forward(FIL* fp, UINT(*func)(const BYTE*, UINT), UINT btf, UINT* bf);
FRESULT f_expand(FIL* fp, FSIZE_t fsz, BYTE opt);
FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt);
FRESULT f_mkfs(const TCHAR* path, const MKFS_PARM* opt, void* work, UINT len);
FRESULT f_fdisk(BYTE pdrv, const LBA_t ptbl[], void* work);
FRESULT f_setcp(WORD cp);
int f_putc(TCHAR c, FIL* fp);
int f_puts(const TCHAR* str, FIL* cp);
int f_printf(FIL* fp, const TCHAR* str, ...);
TCHAR* f_gets(TCHAR* buff, int len, FIL* fp);

#define f_eof(fp)      ((int)((fp)->fptr == (fp)->obj.objsize))
#define f_error(fp)    ((fp)->err)
#define f_tell(fp)     ((fp)->fptr)
#define f_size(fp)     ((fp)->obj.objsize)
#define f_rewind(fp)   f_lseek((fp), 0)
#define f_rewinddir(dp) f_readdir((dp), 0)
#define f_rmdir(path)  f_unlink(path)
#define f_unmount(path) f_mount(0, path, 0)

#if !FF_FS_READONLY && !FF_FS_NORTC
DWORD get_fattime(void);
#endif

#if FF_USE_LFN >= 1
WCHAR ff_oem2uni(WCHAR oem, WORD cp);
WCHAR ff_uni2oem(DWORD uni, WORD cp);
DWORD ff_wtoupper(DWORD uni);
#endif

#if FF_USE_LFN == 3
void* ff_memalloc(UINT msize);
void ff_memfree(void* mblock);
#endif

#if FF_FS_REENTRANT
int ff_mutex_create(int vol);
void ff_mutex_delete(int vol);
int ff_mutex_take(int vol);
void ff_mutex_give(int vol);
#endif

#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_OPEN_EXISTING  0x00
#define FA_CREATE_NEW     0x04
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10
#define FA_OPEN_APPEND    0x30

#define CREATE_LINKMAP    ((FSIZE_t)0 - 1)

#define FM_FAT            0x01
#define FM_FAT32          0x02
#define FM_EXFAT          0x04
#define FM_ANY            0x07
#define FM_SFD            0x08

#define FS_FAT12          1
#define FS_FAT16          2
#define FS_FAT32          3
#define FS_EXFAT          4

#define AM_RDO            0x01
#define AM_HID            0x02
#define AM_SYS            0x04
#define AM_DIR            0x10
#define AM_ARC            0x20

} // namespace fatfs

#endif /* FF_DEFINED */
