/* ISC license. */

#include "hpr.h"

void hpr_wall_seconds (unsigned int secs)
{
  char buf[300] = HPR_WALL_BANNER ;
  size_t n = sizeof(HPR_WALL_BANNER) - 1 ;
  n += hpr_fmttime(buf + n, 300 - n, secs) ;
  buf[n++] = 0 ;
  hpr_wall(buf) ;
}
