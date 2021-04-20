/* ISC license. */

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <sys/reboot.h>
#include <sys/ioctl.h>
#include <linux/kd.h>

#include <skalibs/strerr2.h>
#include <skalibs/sig.h>

#include "os.h"

void os_kbspecials (int inns)
{
  int fd ;
  if (inns) return ;
  fd = open("/dev/tty0", O_RDONLY | O_NOCTTY) ;
  if (fd < 0)
    strerr_warnwu2sys("open /dev/", "tty0 (kbrequest will not be handled)") ;
  else
  {
    if (ioctl(fd, KDSIGACCEPT, SIGWINCH) < 0)
      strerr_warnwu2sys("ioctl KDSIGACCEPT on ", "tty0 (kbrequest will not be handled)") ;
    close(fd) ;
  }

  sig_block(SIGINT) ; /* don't panic on early cad before s6-svscan catches it */
  if (reboot(RB_DISABLE_CAD) == -1)
    strerr_warnwu1sys("trap ctrl-alt-del") ;
}
