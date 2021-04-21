/* ISC license. */

#include <skalibs/nonposix.h>

#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <utmpx.h>

#include <skalibs/strerr2.h>
#include <skalibs/tai.h>

#include "os.h"

#ifndef WTMPX_FILE
# include <utmp.h>
# ifdef WTMP_FILE
#  define WTMPX_FILE WTMP_FILE
# else
# ifdef _PATH_WTMP
#  define WTMPX_FILE _PATH_WTMP
# else
#  define WTMPX_FILE "/var/log/wtmp"
# endif
# endif
#endif

#ifndef UT_NAMESIZE
# define UT_NAMESIZE 32
#endif

#ifndef UT_HOSTSIZE
# define UT_HOSTSIZE 256
#endif

void os_final_wtmp (int what)
{
  struct utmpx utx =
  {
    .ut_type = RUN_LVL,
    .ut_pid = getpid(),
    .ut_line = "~",
    .ut_id = "",
    .ut_session = getsid(0)
  } ;
  strncpy(utx.ut_user, what == 3 ? "reboot" : "shutdown", UT_NAMESIZE) ;
  if (gethostname(utx.ut_host, UT_HOSTSIZE) < 0)
  {
    utx.ut_host[0] = 0 ;
    strerr_warnwu1sys("gethostname") ;
  }
  else utx.ut_host[UT_HOSTSIZE - 1] = 0 ;

 /* glibc multilib can go fuck itself */
#ifdef  __WORDSIZE_TIME64_COMPAT32
  {
    struct timeval tv ;
    if (!timeval_from_tain(&tv, &STAMP))
      strerr_warnwu1sys("timeval_from_tain") ;
    utx.ut_tv.tv_sec = tv.tv_sec ;
    utx.ut_tv.tv_usec = tv.tv_usec ;
  }
#else
  if (!timeval_from_tain(&utx.ut_tv, &STAMP))
    strerr_warnwu1sys("timeval_from_tain") ;
#endif
  updwtmpx(WTMPX_FILE, &utx) ;
}
