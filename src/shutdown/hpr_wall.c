/* ISC license. */

#include <string.h>
#include <utmpx.h>

#include <skalibs/allreadwrite.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/posixishard.h>

#include "hpr.h"

#ifndef UT_LINESIZE
#define UT_LINESIZE 32
#endif

void hpr_wall (char const *s)
{
  size_t n = strlen(s) ;
  char tty[10 + UT_LINESIZE] = "/dev/" ;
  char msg[n+1] ;
  memcpy(msg, s, n) ;
  msg[n++] = '\n' ;
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
    allwrite(fd, msg, n) ;
    fd_close(fd) ;
  }
  endutxent() ;
}
