/* ISC license. */

#ifndef S6_LINUX_INIT_INITCTL_H
#define S6_LINUX_INIT_INITCTL_H

#include <s6-linux-init/config.h>

#define SCANDIRFULL S6_LINUX_INIT_TMPFS "/" S6_LINUX_INIT_SCANDIR

#define SHUTDOWND_SERVICEDIR "s6-linux-init-shutdownd"
#define SHUTDOWND_FIFO "fifo"
#define INITCTL SCANDIRFULL "/" SHUTDOWND_SERVICEDIR "/" SHUTDOWND_FIFO

#define RUNLEVELD_SERVICEDIR "s6-linux-init-runleveld"
#define RUNLEVELD_SOCKET "s"
#define RUNLEVELD_PATH SCANDIRFULL "/" RUNLEVELD_SERVICEDIR "/" RUNLEVELD_SOCKET

#define LOGOUTHOOKD_SERVICEDIR "s6-linux-init-logouthookd"
#define LOGOUTHOOKD_SOCKET "s"
#define LOGOUTHOOKD_PATH SCANDIRFULL "/" LOGOUTHOOKD_SERVICEDIR "/" LOGOUTHOOKD_SOCKET

#define LOGGER_SERVICEDIR "s6-svscan-log"
#define LOGGER_FIFO "fifo"
#define LOGFIFO SCANDIRFULL "/" LOGGER_SERVICEDIR "/" LOGGER_FIFO

#define EARLYGETTY_SERVICEDIR "s6-linux-init-early-getty"
#define EARLYGETTY SCANDIRFULL "/" EARLYGETTY_SERVICEDIR

#define CONTAINER_RESULTS "s6-linux-init-container-results"

#define RUNIMAGE "run-image"
#define ENVSTAGE1 "env"
#define STAGE2 "rc.init"
#define STAGE3 "rc.shutdown"
#define STAGE4 "rc.shutdown.final"

#endif
