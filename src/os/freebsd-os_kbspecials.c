/* ISC license. */

#include "os.h"

 /* FreeBSD hardcodes cad and doesn't have kbrequest */

void os_kbspecials (int inns)
{
  (void)inns ;
}
