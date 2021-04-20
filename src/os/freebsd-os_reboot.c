/* ISC license. */

#include <unistd.h>
#include <sys/reboot.h>

#include "os.h"

void os_reboot (int what)
{
  reboot(what == 3 ? RB_AUTOBOOT : what == 2 ? RB_POWEROFF : RB_HALT) ;
}
