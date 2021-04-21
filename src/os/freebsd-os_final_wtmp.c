/* ISC license. */

#include "os.h"

 /* FreeBSD only does utx.log (wtmp) operations at the rc level. */

void os_final_wtmp (int what)
{
  (void)what ;
}
