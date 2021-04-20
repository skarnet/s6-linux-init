/* ISC license. */

#include <sys/mount.h>

#include "os.h"

int os_mount_devtmpfs (char const *point)
{
  return mount("dev", point, "devtmpfs", MS_NOSUID | MS_NOEXEC, "") ;
}
