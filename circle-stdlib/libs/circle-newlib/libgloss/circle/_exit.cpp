/* Stub version of _exit.  */

#include <limits.h>
#include "config.h"
#include <_ansi.h>
#include <_syslist.h>
#include <circle/startup.h>

extern "C"
void
_exit(int rc)
{
  set_qemu_exit_status(rc);
  halt();
}
