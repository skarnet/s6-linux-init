/* ISC license. */

#include <string.h>

#include <skalibs/types.h>

#include "hpr.h"

size_t hpr_fmttime (char *buf, size_t max, unsigned int seconds)
{
  size_t m = 0 ;
  unsigned int minutes = 0 ;
  unsigned int hours = 0 ;
  if (seconds >= 60)
  {
    minutes = seconds / 60 ;
    seconds %= 60 ;
    if (minutes >= 60)
    {
      hours = minutes / 60 ;
      minutes %= 60 ;
    }
  }
  m = sizeof(HPR_WALL_BANNER) - 1 ;
  if (m > max) return 0 ;
  memcpy(buf, HPR_WALL_BANNER, m) ;
  if (hours)
  {
    if (m + UINT_FMT + 6 > max) return 0 ;
    m += uint_fmt(buf + m, hours) ;
    memcpy(buf + m, " hour", 5) ;
    m += 5 ;
    if (hours > 1) buf[m++] = 's' ;
    buf[m++] = ' ' ;
  }
  if (minutes)
  {
    if (m + UINT_FMT + 8 > max) return 0 ;
    m += uint_fmt(buf + m, minutes) ;
    memcpy(buf + m, " minute", 7) ;
    m += 7 ;
    if (minutes > 1) buf[m++] = 's' ;
    buf[m++] = ' ' ;
  }
  if (m + UINT_FMT + 8 > max) return 0 ;
  m += uint_fmt(buf + m, seconds) ;
  memcpy(buf + m, " second", 7) ;
  m += 7 ;
  if (seconds != 1) buf[m++] = 's' ;
  buf[m++] = ' ' ;
  return m ;
}
