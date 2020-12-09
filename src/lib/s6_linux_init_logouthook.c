/* ISC license. */

#include <skalibs/djbunix.h>
#include <skalibs/socket.h>

#include <s6-linux-init/s6-linux-init.h>
#include "initctl.h"

int s6_linux_init_logouthook (void)
{
  int fd = ipc_stream_nbcoe() ;
  if (fd < 0) return -1 ;
  if (!ipc_timed_connect(fd, LOGOUTHOOKD_PATH, 0, 0))
  {
    fd_close(fd) ;
    return -1 ;
  }
  return fd ;
}
