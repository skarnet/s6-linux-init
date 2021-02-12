/* ISC license. */

#include <string.h>
#include <sys/uio.h>

#include "hpr.h"

void hpr_wall (char const *s)
{
  struct iovec v[2] = { { .iov_base = (char *)s, .iov_len = strlen(s) }, { .iov_base = "\n", .iov_len = 1 } } ;
  hpr_wallv(v, 2) ;
}
