#include "config.h"
#include <_ansi.h>
#include <_syslist.h>
#include <errno.h>

#include "wrap_fatfs.h"

#undef errno
extern int errno;

extern "C"
int
_unlink (const char *name)
{
    FRESULT const fresult = f_unlink (name);

    int result;

    switch (fresult)
    {
        case FR_OK:
            result = 0;
            break;

        default:
            result = -1;
            switch (fresult)
            {
                case FR_NO_PATH:
                    errno = ENOTDIR;
                    break;

                case FR_NO_FILE:
                case FR_INVALID_NAME:
                case FR_INVALID_DRIVE:
                    errno = ENOENT;
                    break;

                case FR_DENIED:
                case FR_WRITE_PROTECTED:
                    errno = EPERM;
                    break;

                case FR_DISK_ERR:
                case FR_INT_ERR:
                case FR_NOT_READY:
                case FR_NOT_ENABLED:
                case FR_NO_FILESYSTEM:
                case FR_TIMEOUT:
                case FR_LOCKED:
                case FR_NOT_ENOUGH_CORE:
                default:
                    errno = EACCES;
                    break;
            }
    }

    return result;
}
