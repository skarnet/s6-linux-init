/* ISC license. */

#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <limits.h>

#include <skalibs/allreadwrite.h>
#include <skalibs/bytestr.h>
#include <skalibs/strerr2.h>

#include "hpr.h"

#define PROMPT "Please enter the machine's hostname: "

void hpr_confirm_hostname (void)
{
  char name[HOST_NAME_MAX + 1] ;
  char buf[HOST_NAME_MAX + 1] ;
  char *p ;
  ssize_t r ;
  if (!isatty(0) || !isatty(1))
    strerr_diefu1x(100, "ask hostname confirmation: stdin or stdout is not a tty") ;
  if (gethostname(name, HOST_NAME_MAX) < 0)
    strerr_diefu1sys(111, "get host name") ;
  name[HOST_NAME_MAX] = 0 ;
  p = strchr(name, '.') ;
  if (p) *p = 0 ;
  if (allwrite(1, PROMPT, sizeof(PROMPT)-1) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
  if (tcdrain(1) < 0)
    strerr_diefu1sys(111, "tcdrain stdout") ;
  if (tcflush(0, TCIFLUSH) < 0)
    strerr_diefu1sys(111, "empty stdin buffer") ;
  r = fd_read(0, buf, HOST_NAME_MAX) ;
  if (!r) errno = EPIPE ;
  if (r <= 0) strerr_diefu1sys(111, "read from stdin") ;
  buf[byte_chr(buf, r, '\n')] = 0 ;
  p = strchr(buf, '.') ;
  if (p) *p = 0 ;
  if (strcasecmp(name, buf))
    strerr_dief2x(1, "hostname mismatch: expecting ", name) ;
}
