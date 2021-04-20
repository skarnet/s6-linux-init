/* ISC license. */

#include <errno.h>

#include <sys/mount.h>

#include <skalibs/strerr2.h>

#include "os.h"

void os_mount_tmpfs (char const *point, unsigned int mounttype)
{
  if (mounttype)
  {
    if (mounttype == 2)
    {
      if (mount("tmpfs", point, "tmpfs", MS_REMOUNT | MS_NODEV | MS_NOSUID, "mode=0755") == -1)
        strerr_diefu2sys(111, "remount ", point) ;
    }
    else
    {
      if (umount(point) == -1)
      {
        if (errno != EINVAL)
          strerr_warnwu2sys("umount ", point) ;
      }
      if (mount("tmpfs", point, "tmpfs", MS_NODEV | MS_NOSUID, "mode=0755") == -1)
        strerr_diefu2sys(111, "mount tmpfs on ", point) ;
    }
  }
}
