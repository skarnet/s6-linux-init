/* ISC license. */

#include <skalibs/nonposix.h>

#include <sys/param.h>
#include <sys/mount.h>

#include "os.h"

int os_mount_devtmpfs (char const *point)
{
  return mount("devfs", point, MNT_NOEXEC | MNT_NOSUID, "") ;
}
