/* ISC license. */

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <utmpx.h>
#include <sys/reboot.h>

#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/sig.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include "defaults.h"
#include "hpr.h"

#ifndef UT_NAMESIZE
#define UT_NAMESIZE 32
#endif

#ifndef UT_HOSTSIZE
#define UT_HOSTSIZE 256
#endif

#ifndef _PATH_WTMP
#define _PATH_WTMP "/dev/null/wtmp"
#endif

#define USAGE PROGNAME " [ -h | -p | -r ] [ -d | -w ] [ -W ] [ -f ]"

int main (int argc, char const *const *argv)
{
  int what = WHATDEFAULT ;
  int force = 0 ;
  int dowtmp = 1 ;
  int dowall = 1 ;
  PROG = PROGNAME ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "hprfdwW", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'h' : what = 1 ; break ;
        case 'p' : what = 2 ; break ;
        case 'r' : what = 3 ; break ;
        case 'f' : force = 1 ; break ;
        case 'd' : dowtmp = 0 ; break ;
        case 'w' : dowtmp = 2 ; break ;
        case 'W' : dowall = 0 ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (geteuid())
  {
    errno = EPERM ;
    strerr_dief1sys(100, "nice try, peon") ;
  }

  if (force)
  {
    reboot(what == 3 ? RB_AUTOBOOT : what == 2 ? RB_POWER_OFF : RB_HALT_SYSTEM) ;
    strerr_diefu1sys(111, "reboot()") ;
  }

  if (!tain_now_g()) strerr_warnw1sys("get current time") ;
  if (dowtmp)
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
    if (!timeval_from_tain(&utx.ut_tv, &STAMP))
      strerr_warnwu1sys("timeval_from_tain") ;
    updwtmpx(_PATH_WTMP, &utx) ;
  }
  if (dowall) hpr_wall_seconds(0) ;
  if (dowtmp < 2)
  {
    if (!hpr_shutdown(what, &STAMP, 0))
      strerr_diefu1sys(111, "notify s6-linux-init-shutdownd") ;
  }
  return 0 ;
}
