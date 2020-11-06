/* ISC license. */

#ifndef HPR_H
#define HPR_H

#include <stddef.h>

#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include "initctl.h"

#define HPR_WALL_BANNER "\n\n*** WARNING ***\nThe system is going down NOW!\n"

#define hpr_send(s, n) openwritenclose_unsafe(INITCTL, (s), n)
#define hpr_cancel() hpr_send("c", 1)
extern int hpr_shutdown (unsigned int, tain_t const *, unsigned int) ;
extern void hpr_wall (char const *) ;
extern void hpr_confirm_hostname (void) ;

#endif
