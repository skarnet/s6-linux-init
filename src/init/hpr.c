/* ISC license. */

#include <skalibs/sysdeps.h>
#include <skalibs/nonposix.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/reboot.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/sig.h>
#include <skalibs/djbunix.h>

#define USAGE PROGNAME " [ -h | -p | -r ] [ -f ]"

#ifdef SKALIBS_HASNSGETPARENT

#include <sys/ioctl.h>
#include <linux/nsfs.h>

static int test_in_namespace (void)
{
  int r ;
  int fd = open_read("/proc/1/ns/pid") ;
  if (fd < 0) return 0 ;
  r = ioctl(myfd, NS_GET_PARENT) ;
  close(fd) ;
  return r >= 0 ;
}

#else

 /*
   When in doubt, always trap signals. This incurs a small race:
   if ctrl-alt-del is pressed at the wrong time, the process will
   exit and cause a kernel panic. But the alternatives are WAY
   more hackish than this.
 */

static int test_in_namespace (void)
{
  return 1 ;
}

#endif

static void sigint_handler (int sig)
{
  (void)sig ;
  _exit(1) ;
}

static void sighup_handler (int sig)
{
  (void)sig ;
  _exit(0) ;
}

int main (int argc, char const *const *argv)
{
  int what = WHATDEFAULT ;
  int force = 0 ;
  PROG = PROGNAME ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "hprf", &l) ;
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

  if (geteuid())
  {
    errno = EPERM ;
    strerr_dief1sys(100, "nice try, peon") ;
  }

  if (force)
  {
    sync() ;
    if (getpid() == 1)
    {
      if (test_in_namespace())
      {
        if (sig_catch(SIGINT, &sigint_handler) < 0
         || sig_catch(SIGHUP, &sighup_handler) < 0)
          strerr_diefu1sys(111, "catch signals") ;
      }
    }
    reboot(what == 3 ? RB_AUTOBOOT : what == 2 ? RB_POWER_OFF : RB_HALT_SYSTEM) ;
    strerr_diefu1sys(111, "reboot()") ;
  }
  else if (kill(1, what == 3 ? SIGINT : what == 2 ? SIGUSR1 : SIGUSR2) < 0)
    strerr_diefu1sys(111, "signal process 1") ;
  return 0 ;
}
