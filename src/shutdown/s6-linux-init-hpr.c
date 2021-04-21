/* ISC license. */

#include <unistd.h>
#include <errno.h>

#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/tai.h>

#include "hpr.h"
#include "os.h"

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
    os_reboot(what) ;
    strerr_diefu1sys(111, "os_reboot") ;
  }

  if (!tain_now_g()) strerr_warnw1sys("get current time") ;
  if (dowtmp) os_final_wtmp(what) ;
  if (dowall) hpr_wall(HPR_WALL_BANNER) ;
  if (dowtmp < 2)
  {
    if (!hpr_shutdown(what, &tain_zero, 0))
      strerr_diefu1sys(111, "notify s6-linux-init-shutdownd") ;
  }
  return 0 ;
}
