/* ISC license. */

#include <sys/reboot.h>

#include "os.h"

void os_reboot (int what)
{
  reboot(what == 3 ? RB_AUTOBOOT : what == 2 ? RB_POWER_OFF : RB_HALT_SYSTEM) ;
}
