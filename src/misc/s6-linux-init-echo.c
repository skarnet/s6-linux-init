/* ISC license. */

#include <skalibs/sgetopt.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>

#define USAGE "s6-linux-init-echo [ -n ] [ -s sep ] args..."

int main (int argc, char const *const *argv)
{
  char sep = ' ' ;
  char donl = 1 ;
  PROG = "s6-linux-init-echo" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "ns:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'n': donl = 0 ; break ;
        case 's': sep = *l.arg ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  for ( ; *argv ; argv++)
    if ((buffer_puts(buffer_1small, *argv) < 0)
     || (argv[1] && (buffer_put(buffer_1small, &sep, 1) < 0)))
      goto err ;
  if (donl && (buffer_put(buffer_1small, "\n", 1) < 0)) goto err ;
  if (!buffer_flush(buffer_1small)) goto err ;
  return 0 ;

 err:
  strerr_diefu1sys(111, "write to stdout") ;
}
