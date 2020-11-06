/* ISC license. */

#include <skalibs/nonposix.h>

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

#define USAGE "s6-linux-init-hpr [ -h | -p | -r ] [ -n ] [ -d | -w ] [ -W ] [ -f ] [ -i ]"

int main (int argc, char const *const *argv)
{
  int what = 0 ;
  int force = 0 ;
  int dowtmp = 1 ;
  int dowall = 1 ;
  int dosync = 1 ;
  int doconfirm = 0 ;
  PROG = "s6-linux-init-hpr" ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "hprfdwWni", &l) ;
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
        case 'n' : dosync = 0 ; break ;
        case 'i' : doconfirm = 1 ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (!what)
    strerr_dief1x(100, "one of the -h, -p or -r options must be given") ;

  if (geteuid())
  {
    errno = EPERM ;
    strerr_dief1sys(100, "nice try, peon") ;
  }

  if (doconfirm) hpr_confirm_hostname() ;

  if (force)
  {
    if (dosync) sync() ;
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

    updwtmpx(_PATH_WTMP, &utx) ;
  }
  if (dowall) hpr_wall(HPR_WALL_BANNER) ;
  if (dowtmp < 2)
  {
    if (!hpr_shutdown(what, &tain_zero, 0))
      strerr_diefu1sys(111, "notify s6-linux-init-shutdownd") ;
  }
  return 0 ;
}
