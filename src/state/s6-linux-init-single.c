/* ISC license. */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <skalibs/allreadwrite.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>

#include "initctl.h"

#define USAGE "s6-linux-init-single"
#define dieusage() strerr_dieusage(100, USAGE)

#define BANNER "Executing interactive root shell.\n"

int main (void)
{
  int fd ;
  PROG = "s6-linux-init-single" ;
  fd = open(EARLYGETTY, O_RDONLY) ;
  if (fd == -1)
  {
    if (errno != ENOENT)
      strerr_diefu2sys(111, "opendir ", EARLYGETTY) ;
    goto shell ;
  }
  if (faccessat(fd, "down", F_OK, 0) == 0)
  {
    fd_close(fd) ;
    goto shell ;
  }
  if (errno != ENOENT)
    strerr_diefu3sys(111, "access ", EARLYGETTY, "/down") ;
  return 0 ;

 shell:

  fd = open("/dev/console", O_RDONLY) ;
  if (fd == -1) strerr_diefu2sys(111, "open /dev/console for ", "reading") ;
  if (fd_move(0, fd) == -1) strerr_diefu1sys(111, "move /dev/console fd to 0") ;
  fd = open("/dev/console", O_WRONLY) ;
  if (fd == -1) strerr_diefu2sys(111, "open /dev/console for ", "writing") ;
  if (fd_move(1, fd) == -1) strerr_diefu1sys(111, "move /dev/console fd to 1") ;
  if (fd_copy(2, 1) == -1) strerr_diefu1sys(111, "dup2 1 to 2") ;
  if (allwrite(1, BANNER, sizeof(BANNER) - 1) < sizeof(BANNER) - 1)
    strerr_diefu1sys(111, "write banner") ;
  {
    char *argv[3] = { "/bin/sh", "-i", 0 } ;
    execv(argv[0], argv) ;
    strerr_dieexec(111, argv[0]) ;
  }
}
