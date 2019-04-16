/* ISC license. */

#include <stdint.h>

#include <skalibs/uint32.h>
#include <skalibs/tai.h>

#include "hpr.h"

int hpr_shutdown (unsigned int what, tain_t const *when, unsigned int grace)
{
  char pack[5 + TAIN_PACK] = { " hpr"[what] } ;
  tain_pack(pack+1, when) ;
  uint32_pack_big(pack + 1 + TAIN_PACK, (uint32_t)grace) ;
  return hpr_send(pack, 5 + TAIN_PACK) ;
}
