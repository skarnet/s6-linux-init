/* ISC license. */

#include <skalibs/sgetopt.h>
#include <skalibs/types.h>
#include <skalibs/strerr2.h>

#include "defaults.h"
#include "initctl.h"
#include "hpr.h"

#define USAGE "s6-linux-init-telinit runlevel"
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv, char const *const *envp)
{
  PROG = "s6-linux-init-telinit" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "rc:p:s:m:d:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'r' :
        case 'c' :
        case 'p' :
        case 's' :
        case 'm' :
        case 'd' : break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (!argc) dieusage() ;
  if (!argv[0][0] || argv[0][1]) dieusage() ;
  if (argv[0][0] != 'S' && (argv[0][0] < '0' || argv[0][0] > '6')) dieusage() ;
  if (!hpr_send(argv[0], 1)) strerr_diefu2sys(111, "write to ", INITCTL) ;
  return 0 ;
}
