/* ISC license. */

#include <string.h>
#include <sys/uio.h>
#include <utmpx.h>

#include <skalibs/allreadwrite.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/posixishard.h>

#include "hpr.h"

#ifndef UT_LINESIZE
#define UT_LINESIZE 32
#endif

void hpr_wallv (struct iovec const *v, unsigned int n)
{
  char tty[10 + UT_LINESIZE] = "/dev/" ;
  setutxent() ;
  for (;;)
  {
    size_t linelen ;
    int fd ;
    struct utmpx *utx = getutxent() ;
    if (!utx) break ;
    if (utx->ut_type != USER_PROCESS) continue ;
    linelen = strnlen(utx->ut_line, UT_LINESIZE) ;
    memcpy(tty + 5, utx->ut_line, linelen) ;
    tty[5 + linelen] = 0 ;
    fd = open_append(tty) ;
    if (fd == -1) continue ;
    allwritev(fd, v, n) ;
    fd_close(fd) ;
  }
  endutxent() ;
}
