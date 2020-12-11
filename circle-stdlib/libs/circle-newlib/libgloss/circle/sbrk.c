/* Version of sbrk for no operating system.  */

#include "config.h"
#include <_syslist.h>
#include <errno.h>

void *
_sbrk (incr)
     int incr;
{
   errno = ENOMEM;

   return (void *) -1;
}
