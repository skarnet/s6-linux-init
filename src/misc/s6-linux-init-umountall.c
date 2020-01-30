/* ISC license. */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <mntent.h>
#include <sys/mount.h>

#include <skalibs/strerr2.h>
#include <skalibs/stralloc.h>
#include <skalibs/skamisc.h>

#include <s6-linux-init/config.h>

#define MAXLINES 99

#define EXCLUDEN 3
static char const *exclude_type[EXCLUDEN] = { "devtmpfs", "proc", "sysfs" } ;

int main (int argc, char const *const *argv)
{
  size_t mountpoints[MAXLINES] ;
  unsigned int got[EXCLUDEN] = { 0, 0, 0 } ;
  stralloc sa = STRALLOC_ZERO ;
  unsigned int line = 0 ;
  int e = 0 ;
  FILE *fp = setmntent("/proc/mounts", "r") ;
  PROG = "s6-linux-init-umountall" ;

  if (!fp) strerr_diefu1sys(111, "open /proc/mounts") ;
  for (;;)
  {
    struct mntent *p ;
    unsigned int i = 0 ;
    errno = 0 ;
    p = getmntent(fp) ;
    if (!p) break ;
    if (!strcmp(p->mnt_dir, S6_LINUX_INIT_TMPFS)) continue ;
    for (; i < EXCLUDEN ; i++)
      if (!strcmp(p->mnt_type, exclude_type[i]))
      {
        got[i]++ ;
        break ;
      }
    if (i < EXCLUDEN && got[i] == 1) continue ;
    if (line >= MAXLINES)
      strerr_dief1x(100, "too many mount points") ;
    mountpoints[line++] = sa.len ;
    if (!stralloc_cats(&sa, p->mnt_dir) || !stralloc_0(&sa))
      strerr_diefu1sys(111, "add mount point to list") ;
  }
  if (errno) strerr_diefu1sys(111, "read /proc/mounts") ;
  endmntent(fp) ;

  while (line--)
    if (umount(sa.s + mountpoints[line]) == -1)
    {
      e++ ;
      strerr_warnwu2sys("umount ", sa.s + mountpoints[line]) ;
    }
  stralloc_free(&sa) ;
  return e ;
}
