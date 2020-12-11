/*
 * Stub version of gettimeofday.
 */

#include "config.h"
#include <_ansi.h>
#include <_syslist.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#undef errno
extern int errno;
#include "warning.h"

#include <circle/timer.h>

extern "C"
int
_gettimeofday(struct timeval *ptimeval, void *ptimezone)
{
        (void) ptimezone;

        unsigned seconds, microSeconds;
        int result;

        if (CTimer::Get ()->GetUniversalTime (&seconds, &microSeconds))
        {
                ptimeval->tv_sec = static_cast<time_t>(seconds);
                ptimeval->tv_usec = static_cast<suseconds_t>(microSeconds);
                result = 0;
        }
        else
        {
                errno = EINVAL;
                result = -1;
        }

        return result;
}
