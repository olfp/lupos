#include "config.h"

#include "wrap_fatfs.h"

#include <_ansi.h>
#include <_syslist.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#undef errno
extern int errno;

#include <circle/input/console.h>
#include <circle/sched/scheduler.h>
#include <circle/usb/usbhostcontroller.h>
#include <circle/string.h>
#include "circle_glue.h"
#include <assert.h>

struct _CIRCLE_DIR
{
    _CIRCLE_DIR ()
        :
        mFirstRead (0), mOpen (0)
    {
        mEntry.d_ino = 0;
        mEntry.d_name[0] = 0;
    }

    FATFS_DIR mCurrentEntry;
    struct dirent mEntry;
    unsigned int mFirstRead :1;
    unsigned int mOpen :1;
};

namespace
{
    FATFS *circle_fat_fs = nullptr;

    constexpr unsigned int MAX_OPEN_FILES = 20;
    constexpr unsigned int MAX_OPEN_DIRS = 20;

    class CGlueIO;
    struct CircleFile
    {
        CircleFile ()
            :
            mCGlueIO (nullptr)
        {
        }
        CGlueIO *mCGlueIO;
    };

    CircleFile fileTab[MAX_OPEN_FILES];
    CSpinLock fileTabLock(TASK_LEVEL);

    _CIRCLE_DIR dirTab[MAX_OPEN_DIRS];
    CSpinLock dirTabLock(TASK_LEVEL);

    /**
     * Helper class to acquire lock and to release it automatically
     * when surrounding block is left.
     */
    class SpinLockHolder
    {
    public:
        SpinLockHolder (CSpinLock &lock) : lockRef(lock)
        {
            lockRef.Acquire ();
        }

        ~SpinLockHolder ()
        {
            lockRef.Release ();
        }

    private:
        CSpinLock &lockRef;
    };

    class CGlueIO
    {
    public:
        virtual
        ~CGlueIO ()
        {
        }

        virtual int
        Read (void *pBuffer, int nCount) = 0;

        virtual int
        Write (const void *pBuffer, int nCount) = 0;

        virtual int
        LSeek(int ptr, int dir) = 0;

        virtual int
        Close (void) = 0;
    };

    class CGlueConsole : public CGlueIO
    {
    public:
        enum TConsoleMode
        {
            ConsoleModeRead, ConsoleModeWrite
        };

        CGlueConsole (CConsole &rConsole, TConsoleMode mode)
            :
            mConsole (rConsole), mMode (mode)
        {
        }

        int
        Read (void *pBuffer, int nCount)
        {
            int nResult = -1;

            if (mMode == ConsoleModeRead)
            {
                CScheduler * const scheduler =
                    CScheduler::IsActive () ? CScheduler::Get () : nullptr;
                CUSBHostController * const usbhost =
                    CUSBHostController::IsActive () ?
                        CUSBHostController::Get () : nullptr;

                while ((nResult = mConsole.Read (pBuffer,
                                                 static_cast<size_t> (nCount)))
                    == 0)
                {
                    if (scheduler)
                    {
                        scheduler->Yield ();
                    }

                    if (usbhost && usbhost->UpdatePlugAndPlay ())
                    {
                        mConsole.UpdatePlugAndPlay ();
                    }
                }
            }

            if (nResult < 0)
            {
                errno = EIO;

                // Could be negative but different from -1.
                nResult = -1;
            }

            return nResult;
        }

        int
        Write (const void *pBuffer, int nCount)
        {
            int nResult =
                mMode == ConsoleModeWrite ?
                    mConsole.Write (pBuffer, static_cast<size_t> (nCount)) : -1;

            if (nResult < 0)
            {
                errno = EIO;

                // Could be negative but different from -1.
                nResult = -1;
            }

            return nResult;
        }

        int
        LSeek(int ptr, int dir)
        {
            errno = ESPIPE;
            return -1;
        }

        int
        Close (void)
        {
            // TODO: Cannot close console file handle currently.
            errno = EBADF;
            return -1;
        }

    private:
        CConsole &mConsole;
        TConsoleMode const mMode;
    };

    /**
     * FatFs: http://www.elm-chan.org/fsw/ff/00index_e.html
     */
    struct CGlueIoFatFs : public CGlueIO
    {
        CGlueIoFatFs ()
        {
            memset (&mFile, 0, sizeof(mFile));
        }

        bool
        Open (char *file, int flags, int /* mode */)
        {
            int fatfs_flags;

            /*
             * The OpenGroup documentation of the flags for open() says:
             *
             * "Applications shall specify exactly one of the first
             * three values (file access modes) below in the value of oflag:
             *
             * O_RDONLY Open for reading only.
             * O_WRONLY Open for writing only.
             * O_RDWR Open for reading and writing."
             *
             * So combinations of those flags need not to be dealt with.
             */
            if (flags & O_RDWR)
            {
                fatfs_flags = FA_READ | FA_WRITE;
            }
            else if (flags & O_WRONLY)
            {
                fatfs_flags = FA_WRITE;
            }
            else
            {
                fatfs_flags = FA_READ;
            }

            if (flags & O_TRUNC)
            {
                /*
                 * OpenGroup documentation:
                 * "The result of using O_TRUNC with O_RDONLY is undefined."
                 */
                fatfs_flags |= FA_CREATE_ALWAYS;
            }

            if (flags & O_APPEND)
            {
                fatfs_flags |= FA_OPEN_APPEND;
            }

            if (flags & O_CREAT)
            {
                if (flags & O_EXCL)
                {
                    /*
                     * OpenGroup documentation:
                     * "O_EXCL If O_CREAT and O_EXCL are set, open() shall fail if the file exists.
                     * "If O_EXCL is set and O_CREAT is not set, the result is undefined.".
                     */
                    fatfs_flags |= FA_CREATE_NEW;
                }
                else
                {
                    fatfs_flags |= FA_OPEN_ALWAYS;
                }
            }

            // TODO mode?
            auto const fresult = f_open (&mFile, file, fatfs_flags);

            bool result;
            if (fresult == FR_OK)
            {
                result = true;
            }
            else
            {
                result = false;

                switch (fresult)
                {
                    case FR_EXIST:
                        errno = EEXIST;
                        break;

                    case FR_NO_FILE:
                    case FR_INVALID_NAME:
                        errno = ENOENT;
                        break;

                    case FR_NO_PATH:
                    case FR_INVALID_DRIVE:
                        errno = ENOTDIR;
                        break;

                    case FR_NOT_ENOUGH_CORE:
                        errno = ENOMEM;
                        break;

                    case FR_TOO_MANY_OPEN_FILES:
                        errno = ENFILE;
                        break;

                    case FR_DENIED:
                    case FR_DISK_ERR:
                    case FR_INT_ERR:
                    case FR_NOT_READY:
                    case FR_INVALID_OBJECT:
                    case FR_WRITE_PROTECTED:
                    case FR_NOT_ENABLED:
                    case FR_NO_FILESYSTEM:
                    case FR_TIMEOUT:
                    case FR_LOCKED:
                    default:
                        errno = EACCES;
                        break;
                }
            }

            return result;
        }

        int
        Read (void *pBuffer, int nCount)
        {
            UINT bytes_read = 0;
            auto const fresult = f_read (&mFile, pBuffer,
                                         static_cast<UINT> (nCount),
                                         &bytes_read);

            int result;
            if (fresult == FR_OK)
            {
                result = static_cast<int> (bytes_read);
            }
            else
            {
                result = -1;

                switch (fresult)
                {
                    case FR_INVALID_OBJECT:
                    case FR_DENIED:
                        errno = EBADF;
                        break;

                    case FR_DISK_ERR:
                    case FR_INT_ERR:
                    case FR_TIMEOUT:
                    default:
                        errno = EIO;
                        break;
                }
            }

            return result;
        }

        int
        Write (const void *pBuffer, int nCount)
        {
            UINT bytesWritten = 0;
            auto const fresult = f_write (&mFile, pBuffer,
                                          static_cast<UINT> (nCount),
                                          &bytesWritten);

            int result;
            if (fresult == FR_OK)
            {
                result = static_cast<int> (bytesWritten);
            }
            else
            {
                result = -1;

                switch (fresult)
                {
                    case FR_INVALID_OBJECT:
                        errno = EBADF;
                        break;

                    case FR_DISK_ERR:
                    case FR_INT_ERR:
                    case FR_DENIED:
                    case FR_TIMEOUT:
                    default:
                        errno = EIO;
                        break;
                }
            }

            return result;
        }

        int
        LSeek(int ptr, int dir)
        {
            /*
             * If whence is SEEK_SET, the file offset shall be set to
             * offset bytes.
             *
             * If whence is SEEK_CUR, the file offset shall be set to
             * its current location plus offset.
             *
             * If whence is SEEK_END, the file offset shall be set to
             * the size of the file plus offset.
             */
            int new_pos;
            switch (dir)
            {
                case SEEK_SET:
                    new_pos = ptr;
                    break;

                case SEEK_CUR:
                    new_pos = static_cast<int>(f_tell(&mFile)) + ptr;
                    break;

                case SEEK_END:
                    new_pos = static_cast<int>(f_size(&mFile)) + ptr;
                    break;

                default:
                    new_pos = -1;
                    break;
            }

            int result;
            if (new_pos >= 0)
            {
                int const fresult = f_lseek(&mFile, static_cast<FSIZE_t>(new_pos));

                switch (fresult)
                {
                    case FR_OK:
                        result = new_pos;
                        break;

                    case FR_DISK_ERR:
                    case FR_INT_ERR:
                    case FR_INVALID_OBJECT:
                    case FR_TIMEOUT:
                    default:
                        result = -1;
                        errno = EBADF;
                        break;
                }
            }
            else
            {
                errno = EINVAL;
                result = -1;
            }

            return result;
        }

        int
        Close (void)
        {
            auto const close_result = f_close (&mFile);

            int result;
            switch (close_result)
            {
                case FR_OK:
                    result = 0;
                    break;

                case FR_INVALID_OBJECT:
                case FR_INT_ERR:
                    errno = EBADF;
                    result = -1;
                    break;

                default:
                    errno = EIO;
                    result = -1;
                    break;
            }

            return result;
        }

        FIL mFile;
    };

    int
    FindFreeFileSlot (void)
    {
        int slotNr = -1;

        for (auto const &slot : fileTab)
        {
            if (slot.mCGlueIO == nullptr)
            {
                slotNr = &slot - fileTab;
                break;
            }
        }

        return slotNr;
    }

    int
    FindFreeDirSlot (void)
    {
        int slotNr = -1;

        for (auto const &slot : dirTab)
        {
            if (!slot.mOpen)
            {
                slotNr = &slot - dirTab;
                break;
            }
        }

        return slotNr;
    }

    void
    CGlueInitFileSystem (FATFS &rFATFileSystem)
    {
        // Must only be called once
        assert(!circle_fat_fs);

        circle_fat_fs = &rFATFileSystem;
    }

    void
    CGlueInitConsole (CConsole &rConsole)
    {
        CircleFile &stdin = fileTab[0];
        CircleFile &stdout = fileTab[1];
        CircleFile &stderr = fileTab[2];

        // Must only be called once and not be called after a file has already been opened
        assert(!stdin.mCGlueIO);
        assert(!stdout.mCGlueIO);
        assert(!stderr.mCGlueIO);

        stdin.mCGlueIO = new CGlueConsole (rConsole,
                                           CGlueConsole::ConsoleModeRead);
        stdout.mCGlueIO = new CGlueConsole (rConsole,
                                            CGlueConsole::ConsoleModeWrite);
        stderr.mCGlueIO = new CGlueConsole (rConsole,
                                            CGlueConsole::ConsoleModeWrite);
    }
}

void
CGlueStdioInit (FATFS &rFATFileSystem, CConsole &rConsole)
{
    CGlueInitConsole (rConsole);
    CGlueInitFileSystem (rFATFileSystem);
}

void
CGlueStdioInit (FATFS &rFATFileSystem)
{
    CGlueInitFileSystem (rFATFileSystem);
}

void
CGlueStdioInit (CConsole &rConsole)
{
    CGlueInitConsole (rConsole);
}

extern "C" int
_open (char *file, int flags, int mode)
{
    SpinLockHolder lockHolder(fileTabLock);

    int slot = FindFreeFileSlot ();

    if (slot != -1)
    {
        auto const newFatFs = new CGlueIoFatFs ();

        if (newFatFs->Open (file, flags, mode))
        {
            fileTab[slot].mCGlueIO = newFatFs;
        }
        else
        {
            delete newFatFs;
	    slot = -1;
        }
    }
    else
    {
        errno = ENFILE;
    }

    return slot;
}

extern "C" int
_close (int fildes)
{
    if (fildes < 0 || static_cast<unsigned int> (fildes) >= MAX_OPEN_FILES)
    {
        errno = EBADF;
        return -1;
    }

    SpinLockHolder lockHolder(fileTabLock);

    CircleFile &file = fileTab[fildes];
    if (file.mCGlueIO == nullptr)
    {
        errno = EBADF;
        return -1;
    }

    unsigned const result = file.mCGlueIO->Close ();

    delete file.mCGlueIO;
    file.mCGlueIO = nullptr;

    return result;
}

extern "C" int
_read (int fildes, char *ptr, int len)
{
    if (fildes < 0 || static_cast<unsigned int> (fildes) >= MAX_OPEN_FILES)
    {
        errno = EBADF;
        return -1;
    }

    CircleFile &file = fileTab[fildes];
    if (file.mCGlueIO == nullptr)
    {
        errno = EBADF;
        return -1;
    }

    return file.mCGlueIO->Read (ptr, len);
}

extern "C" int
_write (int fildes, char *ptr, int len)
{
    if (fildes < 0 || static_cast<unsigned int> (fildes) >= MAX_OPEN_FILES)
    {
        errno = EBADF;
        return -1;
    }

    CircleFile &file = fileTab[fildes];
    if (file.mCGlueIO == nullptr)
    {
        errno = EBADF;
        return -1;
    }

    return file.mCGlueIO->Write (ptr, len);
}

extern "C" int
_lseek(int fildes, int ptr, int dir)
{
    if (fildes < 0 || static_cast<unsigned int> (fildes) >= MAX_OPEN_FILES)
    {
        errno = EBADF;
        return -1;
    }

    CircleFile &file = fileTab[fildes];
    if (file.mCGlueIO == nullptr)
    {
        errno = EBADF;
        return -1;
    }

    return file.mCGlueIO->LSeek (ptr, dir);
}

extern "C" DIR*
opendir (const char *name)
{
    assert(circle_fat_fs);

    SpinLockHolder lockHolder(dirTabLock);

    int const slotNum = FindFreeDirSlot ();
    if (slotNum == -1)
    {
        errno = ENFILE;
        return nullptr;
    }

    auto &slot = dirTab[slotNum];

    FRESULT const fresult = f_opendir (&slot.mCurrentEntry, name);

    /*
     * Best-effort mapping of FatFs error codes to errno values.
     */
    DIR *result = nullptr;
    switch (fresult)
    {
        case FR_OK:
            slot.mOpen = 1;
            slot.mFirstRead = 1;
            result = &slot;
            break;

        case FR_DISK_ERR:
        case FR_INT_ERR:
        case FR_NOT_READY:
        case FR_INVALID_OBJECT:
            errno = EACCES;
            break;

        case FR_NO_PATH:
        case FR_INVALID_NAME:
        case FR_INVALID_DRIVE:
        case FR_NOT_ENABLED:
        case FR_NO_FILESYSTEM:
        case FR_TIMEOUT:
            errno = ENOENT;
            break;

        case FR_NOT_ENOUGH_CORE:
        case FR_TOO_MANY_OPEN_FILES:
            errno = ENFILE;
            break;
    }

    return result;
}

static bool
read_next_entry (FATFS_DIR *currentEntry, FILINFO *filinfo)
{
    bool result;

    if (f_readdir (currentEntry, filinfo) == FR_OK)
    {
        result = filinfo->fname[0] != 0;
    }
    else
    {
        errno = EBADF;
        result = false;
    }

    return result;
}

static struct dirent*
do_readdir (DIR *dir, struct dirent *de)
{
    FILINFO filinfo;
    bool haveEntry;

    if (dir->mFirstRead)
    {
        if (f_readdir (&dir->mCurrentEntry, nullptr) == FR_OK)
        {
            haveEntry = read_next_entry (&dir->mCurrentEntry, &filinfo);
        }
        else
        {
            errno = EBADF;
            haveEntry = false;
        }
        dir->mFirstRead = 0;
    }
    else
    {
        haveEntry = read_next_entry (&dir->mCurrentEntry, &filinfo);
    }

    struct dirent *result;
    if (haveEntry)
    {
        memcpy (de->d_name, filinfo.fname, sizeof(de->d_name));
        de->d_ino = 0; // inode number does not exist in fatfs
        result = de;
    }
    else
    {
        // end of directory does not change errno
        result = nullptr;
    }

    return result;
}

extern "C" struct dirent*
readdir (DIR *dir)
{
    struct dirent *result;

    if (dir->mOpen)
    {
        result = do_readdir (dir, &dir->mEntry);
    }
    else
    {
        errno = EBADF;
        result = nullptr;
    }

    return result;
}

extern "C" int
readdir_r (DIR *__restrict dir, dirent *__restrict de, dirent **__restrict ode)
{
    int result;

    if (dir->mOpen)
    {
        *ode = do_readdir (dir, de);
        result = 0;
    }
    else
    {
        *ode = nullptr;
        result = EBADF;
    }

    return result;
}

extern "C" void
rewinddir (DIR *dir)
{
    dir->mFirstRead = 1;
}

extern "C" int
closedir (DIR *dir)
{
    SpinLockHolder lockHolder(dirTabLock);

    int result;

    if (dir->mOpen)
    {
        dir->mOpen = 0;

        if (f_closedir (&dir->mCurrentEntry) == FR_OK)
        {
            result = 0;
        }
        else
        {
            errno = EBADF;
            result = -1;
        }
    }
    else
    {
        errno = EBADF;
        result = -1;
    }

    return result;
}
