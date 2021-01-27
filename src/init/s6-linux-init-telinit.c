/* ISC license. */

#include <string.h>
#include <sys/wait.h>

#include <skalibs/types.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/exec.h>

#include <s6/config.h>

#include <s6-linux-init/config.h>
#include "initctl.h"

#define USAGE "s6-linux-init-telinit runlevel"
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv, char const *const *envp)
{
  char const *newargv[8] = { S6_EXTBINPREFIX "s6-sudo", "-e", "-T", "3600000", "--", RUNLEVELD_PATH, 0, 0 } ;
  PROG = "s6-linux-init-telinit" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "c:p:s:m:d:D:nNCB", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : /* s6-linux-init may be called with these options, don't choke on them */
        case 'p' :
        case 's' :
        case 'm' :
        case 'd' :
        case 'D' :
        case 'n' :
        case 'N' :
        case 'C' :
        case 'B' :
          break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (!argc) dieusage() ;
  newargv[6] = argv[0] ;


 /* specialcase 0 and 6: fork runlevel call then exec shutdown, instead of execing runlevel call */

  if (!strcmp(argv[0], "0") || !strcmp(argv[0], "6"))
  {
    int wstat ;
    pid_t pid = child_spawn0(newargv[0], newargv, envp) ;
    if (!pid) strerr_diefu2sys(111, "spawn ", newargv[0]) ;
    if (wait_pid(pid, &wstat) < 0) strerr_diefu1sys(111, "wait_pid") ;
    if (WIFSIGNALED(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
      strerr_dief3x(wait_estatus(wstat), newargv[0], " crashed with signal ", fmt) ;
    }
    else if (WEXITSTATUS(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
      strerr_dief3x(wait_estatus(wstat), newargv[0], " died with exitcode ", fmt) ;
    }
   
    newargv[0] = S6_LINUX_INIT_BINPREFIX "s6-linux-init-hpr" ; 
    newargv[1] = argv[0][0] == '6' ? "-r" : "-p" ;
    newargv[2] = 0 ;
  }

  xexec_e(newargv, envp) ;
}
