#include <unistd.h>
#include <errno.h>
#include "wrap_fatfs.h"

#undef errno
extern int errno;

extern "C"
int
mkdir (const char *path, mode_t mode)
{
    (void) mode;

    FRESULT const fresult = f_mkdir (path);

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
                case FR_INVALID_NAME:
                case FR_INVALID_DRIVE:
                    errno = ENOENT;
                    break;

                case FR_DENIED:
                case FR_WRITE_PROTECTED:
                    errno = EPERM;
                    break;

                case FR_EXIST:
                    errno = EEXIST;
                    break;

                case FR_DISK_ERR:
                case FR_INT_ERR:
                case FR_NOT_READY:
                case FR_NOT_ENABLED:
                case FR_NO_FILESYSTEM:
                case FR_TIMEOUT:
                case FR_NOT_ENOUGH_CORE:
                default:
                    errno = EACCES;
                    break;
            }
    }

    return result;
}
