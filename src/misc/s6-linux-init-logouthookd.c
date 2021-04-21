/* ISC license. */

#include <skalibs/sysdeps.h>

#ifdef SKALIBS_HASSOPEERCRED

#include <skalibs/nonposix.h>

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <utmpx.h>

#include <skalibs/allreadwrite.h>
#include <skalibs/strerr2.h>

#ifndef UT_NAMESIZE
#define UT_NAMESIZE 32
#endif

#ifndef UT_HOSTSIZE
#define UT_HOSTSIZE 256
#endif

int main (void)
{
  struct utmpx *utx ;
  struct ucred client ;
  socklen_t len = sizeof(client) ;
  ssize_t r ;
  char c ;

  close(1) ;
  if (getsockopt(0, SOL_SOCKET, SO_PEERCRED, &client, &len) == -1)
    strerr_diefu1sys(111, "getsockopt") ;

 /* Only take connections from root. */
  if (client.uid) return 1 ;

 /* Wait for the client to die. */
  r = fd_read(0, &c, 1) ;
  if (r < 0) strerr_diefu1sys(111, "read from stdin") ;
  if (r) strerr_dief1x(2, "client attempted to write") ;

 /* Clean up utmpx record for the client's pid, then exit. */
  for (;;)
  {
    errno = 0 ;
    utx = getutxent() ;
    if (!utx) break ;
    if (utx->ut_pid == client.pid) goto gotit ;
  }
  if (errno) strerr_diefu1sys(111, "getutxent") ;
  return 0 ;

 gotit:
  utx->ut_type = DEAD_PROCESS ;
  memset(utx->ut_user, 0, UT_NAMESIZE) ;
  memset(utx->ut_host, 0, UT_HOSTSIZE) ;
  utx->ut_tv.tv_sec = 0 ;
  utx->ut_tv.tv_usec = 0 ;
  setutxent() ;
  if (!pututxline(utx)) strerr_diefu1sys(111, "pututxline") ;
  return 0 ;
}

#else

 /* Only Linux needs a real implementation */

int main (void)
{
}

#endif
