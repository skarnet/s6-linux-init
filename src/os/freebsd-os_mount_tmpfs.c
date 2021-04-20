/* ISC license. */

#include <skalibs/nonposix.h>

#include <errno.h>

#include <sys/param.h>
#include <sys/mount.h>

#include <skalibs/strerr2.h>

#include "os.h"

void os_mount_tmpfs (char const *point, unsigned int mounttype)
{
  if (mounttype)
  {
    if (mounttype == 2)
    {
      if (mount("tmpfs", point, MNT_UPDATE | MNT_NOSUID, "mode=0755") == -1)
        strerr_diefu2sys(111, "remount ", point) ;
    }
    else
    {
      if (unmount(point, MNT_FORCE) == -1)
      {
        if (errno != EINVAL)
          strerr_warnwu2sys("umount ", point) ;
      }
      if (mount("tmpfs", point, MNT_NOSUID, "mode=0755") == -1)
        strerr_diefu2sys(111, "mount tmpfs on ", point) ;
    }
  }
}
