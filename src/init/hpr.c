/* ISC license. */

#include <unistd.h>
#include <signal.h>
#include <sys/reboot.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>

#define USAGE PROGNAME " [ -h | -p | -r ] [ -f ]"

int main (int argc, char const *const *argv)
{
  int what = WHATDEFAULT ;
  int force = 0 ;
  PROG = PROGNAME ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "hprf", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'h' : what = 1 ; break ;
        case 'p' : what = 2 ; break ;
        case 'r' : what = 3 ; break ;
        case 'f' : force = 1 ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (force)
  {
    sync() ;
    reboot(what == 3 ? RB_AUTOBOOT : what == 2 ? RB_POWER_OFF : RB_HALT_SYSTEM) ;
    strerr_diefu1sys(111, "reboot()") ;
  }
  else if (kill(1, what == 3 ? SIGINT : what == 2 ? SIGUSR1 : SIGUSR2) < 0)
    strerr_diefu1sys(111, "signal process 1") ;
  return 0 ;
}
