/* ISC license. */

#ifndef S6_LINUX_INIT_OS_H
#define S6_LINUX_INIT_OS_H

extern void os_reboot (int) ;
extern void os_kbspecials (int) ;
extern void os_mount_tmpfs (char const *, unsigned int) ;
extern int os_mount_devtmpfs (char const *) ;

#endif
