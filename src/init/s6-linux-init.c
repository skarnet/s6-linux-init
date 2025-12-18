/* ISC license. */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/ioctl.h>

#include <linux/kd.h>

#include <skalibs/stat.h>
#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/envexec.h>
#include <skalibs/sig.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>

#include <s6/config.h>

#include <s6-linux-init/config.h>
#include "defaults.h"
#include "initctl.h"

#define USAGE "s6-linux-init [ -v verbosity ] [ -c basedir ] [ -p initpath ] [ -s envdumpdir ] [ -m umask ] [ -d devtmpfs ] [ -D initdefault ] [ -n | -N ] [ -C ] [ -B ]"
#define dieusage() strerr_dieusage(100, USAGE)

#define BANNER "\n  s6-linux-init version " S6_LINUX_INIT_VERSION "\n\n"

enum golb_e
{
  GOLB_HANDSOFFRUN = 0x01,
  GOLB_NOUNMOUNTRUN = 0x02,
  GOLB_INNS = 0x04,
  GOLB_NOLOGGER = 0x08,
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_BASEDIR,
  GOLA_PATH,
  GOLA_ENVDUMPDIR,
  GOLA_MASK,
  GOLA_SLASHDEV,
  GOLA_INITDEFAULT,
  GOLA_N
} ;

static int notifpipe[2] ;
static unsigned int verbosity = 1 ;

static inline char const *scan_cmdline (char const *initdefault, char const *const *argv, unsigned int argc, int inns)
{
  if (!inns)
  {
    static char const *valid[] = { "default", "1", "2", "3", "4", "5", 0 } ;
    for (unsigned int i = 0 ; i < argc ; i++)
      for (char const *const *p = valid ; *p ; p++)
        if (!strcmp(argv[i], *p)) return argv[i] ;
  }
  return initdefault ;
}

static inline void wait_for_notif (int fd)
{
  char buf[16] ;
  for (;;)
  {
    ssize_t r = read(fd, buf, 16) ;
    if (r < 0) strerr_diefu1sys(111, "read from notification pipe") ;
    if (!r)
    {
      if (verbosity) strerr_warnw1x("s6-svscan failed to send a notification byte!") ;
      break ;
    }
    if (memchr(buf, '\n', r)) break ;
  }
  close(fd) ;
}

static void kbspecials (void)
{
  int fd = open2("/dev/tty0", O_RDONLY | O_NOCTTY) ;
  if (fd == -1)
  {
    if (errno == ENOENT)
    {
      if (verbosity >= 2) strerr_warni1x("headless system detected") ;
    }
    else if (verbosity) strerr_warnwu2sys("open", " tty0 (kbrequest will not be handled)") ;
  }
  else
  {
    if (ioctl(fd, KDSIGACCEPT, SIGWINCH) < 0)
      if (verbosity) strerr_warnwu2sys("ioctl KDSIGACCEPT on", " tty0 (kbrequest will not be handled)") ;
    close(fd) ;
  }

  sig_block(SIGINT) ; /* don't panic on early cad before s6-svscan catches it */
  if (reboot(RB_DISABLE_CAD) == -1)
    if (verbosity) strerr_warnwu1sys("trap ctrl-alt-del") ;
}

static void opendevnull (void)
{
  if (open2("/dev/null", O_RDONLY))
  {  /* ghetto /dev/null to the rescue */
    int p[2] ;
    if (verbosity) strerr_warnwu1sys("open /dev/null") ;
    if (pipe(p) < 0) strerr_diefu1sys(111, "pipe") ;
    close(p[1]) ;
    if (fd_move(0, p[0]) < 0) strerr_diefu1sys(111, "fd_move to stdin") ;
  }
}

static void reset_stdin (void)
{
  close(0) ;
  opendevnull() ;
}

static inline void run_stage2 (char const *basedir, char const **argv, unsigned int argc, char const *const *envp, size_t envlen, char const *modifs, size_t modiflen, char const *initdefault, char const *tty, uint64_t wgolb)
{
  size_t dirlen = strlen(basedir) ;
  char const *childargv[argc + 3] ;
  char fn[dirlen + sizeof("/scripts/" STAGE2)] ;
  PROG = "s6-linux-init (child)" ;

  if (setsid() == -1) strerr_diefu1sys(111, "setsid") ;
  if (tty)
  {
    close(0) ;
    if (openb_read(tty))
    {
      if (verbosity) strerr_warnwu2sys("open ", tty) ;
      opendevnull() ;
    }
  }
  memcpy(fn, basedir, dirlen) ;
  memcpy(fn + dirlen, "/scripts/" STAGE2, sizeof("/scripts/" STAGE2)) ;
  childargv[0] = fn ;
  childargv[1] = scan_cmdline(initdefault, argv, argc, wgolb & GOLB_INNS) ;
  for (unsigned int i = 0 ; i < argc ; i++)
    childargv[i+2] = argv[i] ;
  childargv[argc + 2] = 0 ;
  if (wgolb & GOLB_NOLOGGER)
  {
    close(notifpipe[1]) ;
    wait_for_notif(notifpipe[0]) ;
  }
  else
  {
   /* block on opening the log fifo until the catch-all logger is up */
    close(1) ;
    if (open2(LOGFIFO, O_WRONLY) != 1)
      strerr_diefu1sys(111, "open " LOGFIFO " for writing") ;
    if (fd_copy(2, 1) == -1)
      strerr_diefu1sys(111, "fd_copy stdout to stderr") ;
  }
  xmexec_fm(childargv, envp, envlen, modifs, modiflen) ;
}

int main (int argc, char const **argv, char const *const *envp)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'n', .lo = "no-unmount-slashrun", .clear = GOLB_HANDSOFFRUN, .set = GOLB_NOUNMOUNTRUN },
    { .so = 'N', .lo = "hands-off-slashrun", .clear = 0, .set = GOLB_HANDSOFFRUN },
    { .so = 'C', .lo = "container", .clear = 0, .set = GOLB_INNS },
    { .so = 'B', .lo = "no-logger", .clear = 0, .set = GOLB_NOLOGGER },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'c', .lo = "basedir", .i = GOLA_BASEDIR },
    { .so = 'p', .lo = "initial-path", .i = GOLA_PATH },
    { .so = 's', .lo = "envdumpdir", .i = GOLA_ENVDUMPDIR },
    { .so = 'm', .lo = "umask", .i = GOLA_MASK },
    { .so = 'd', .lo = "slashdev", .i = GOLA_SLASHDEV },
    { .so = 'D', .lo = "default-runlevel", .i = GOLA_INITDEFAULT },
  } ;
  char const *wgola[GOLA_N] =
  {
    [GOLA_VERBOSITY] = 0,
    [GOLA_BASEDIR] = BASEDIR,
    [GOLA_PATH] = INITPATH,
    [GOLA_ENVDUMPDIR] = 0,
    [GOLA_MASK] = 0,
    [GOLA_SLASHDEV] = 0,
    [GOLA_INITDEFAULT] = "default",
  } ;
  uint64_t wgolb = 0 ;
  mode_t mask = 0022 ;
  stralloc envmodifs = STRALLOC_ZERO ;
  char *tty = 0 ;
  unsigned int golc ;
  int hasconsole ;
  PROG = "s6-linux-init" ;

  if (getpid() != 1)
  {
    argv[0] = S6_LINUX_INIT_BINPREFIX "s6-linux-init-telinit" ;
    xexec_e(argv, envp) ;
  }

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity)) dieusage() ;
  if (wgola[GOLA_MASK] && !uint0_oscan(wgola[GOLA_MASK], &mask)) dieusage() ;

  hasconsole = fcntl(1, F_GETFD) >= 0 ;
  if (wgolb & GOLB_INNS)
  {
    char c ;
    ssize_t r = read(3, &c, 1) ; /* Docker synchronization protocol */
    if (r < 0)
    {
      if (errno != EBADF) strerr_diefu1sys(111, "read from fd 3") ;
    }
    else
    {
      if (r) if (verbosity) strerr_warnw1x("parent wrote to fd 3!") ;
      close(3) ;
    }
    if (!wgola[GOLA_SLASHDEV] && hasconsole && isatty(1 + !(wgolb & GOLB_NOLOGGER)))
    {
      tty = ttyname(1 + !(wgolb & GOLB_NOLOGGER)) ;
      if (!tty) if (verbosity) strerr_warnwu2sys("ttyname std", wgolb & GOLB_NOLOGGER ? "err" : "out") ;
    }
  }
  else if (hasconsole) allwrite(1, BANNER, sizeof(BANNER) - 1) ;
  if (chdir("/") == -1) strerr_diefu1sys(111, "chdir to /") ;
  umask(mask) ;

  if (wgola[GOLA_SLASHDEV])
  {
    int nope, e ;
    close(0) ;
    close(1) ;
    close(2) ;
   /* at this point we're totally in the dark, hoping /dev/console will work */
    nope = mount("dev", wgola[GOLA_SLASHDEV], "devtmpfs", MS_NOSUID | MS_NOEXEC, "") < 0 ;
    e = errno ;
    if (open2("/dev/console", O_WRONLY) && open2("/dev/null", O_WRONLY)) return 111 ;
    if (fd_move(2, 0) < 0) return 111 ;
    if (fd_copy(1, 2) < 0) strerr_diefu1sys(111, "fd_copy") ;
    if (nope)
    {
      errno = e ;
      strerr_diefu1sys(111, "mount a devtmpfs on /dev") ;
    }
    if (open2("/dev/console", O_RDONLY)) opendevnull() ;
  }

  if (!hasconsole)
  {
    if (!wgola[GOLA_SLASHDEV]) reset_stdin() ;
    if (open2("/dev/null", O_WRONLY) != 1 || fd_copy(2, 1) == -1)
      return 111 ;
  }

  if (!(wgolb & GOLB_HANDSOFFRUN))
  {
    if (wgolb & GOLB_NOUNMOUNTRUN)
    {
      if (mount("tmpfs", S6_LINUX_INIT_TMPFS, "tmpfs", MS_REMOUNT | MS_NODEV | MS_NOSUID, "mode=0755") == -1)
        strerr_diefu1sys(111, "remount " S6_LINUX_INIT_TMPFS) ;
    }
    else
    {
      if (umount(S6_LINUX_INIT_TMPFS) == -1)
      {
        if (errno != EINVAL)
          if (verbosity) strerr_warnwu1sys("umount " S6_LINUX_INIT_TMPFS) ;
      }
      if (mount("tmpfs", S6_LINUX_INIT_TMPFS, "tmpfs", MS_NODEV | MS_NOSUID, "mode=0755") == -1)
        strerr_diefu1sys(111, "mount tmpfs on " S6_LINUX_INIT_TMPFS) ;
    }
  }

  {
    size_t dirlen = strlen(wgola[GOLA_BASEDIR]) ;
    char fn[dirlen + 1 + (sizeof(RUNIMAGE) > sizeof(ENVSTAGE1) ? sizeof(RUNIMAGE) : sizeof(ENVSTAGE1))] ;
    memcpy(fn, wgola[GOLA_BASEDIR], dirlen) ;
    fn[dirlen] = '/' ;
    memcpy(fn + dirlen + 1, RUNIMAGE, sizeof(RUNIMAGE)) ;
    if (!hiercopy_loose(fn, S6_LINUX_INIT_TMPFS))
      strerr_diefu3sys(111, "copy ", fn, " to " S6_LINUX_INIT_TMPFS) ;
    memcpy(fn + dirlen + 1, ENVSTAGE1, sizeof(ENVSTAGE1)) ;
    if (envdir_internal(fn, &envmodifs, SKALIBS_ENVDIR_VERBATIM | SKALIBS_ENVDIR_NOCLAMP, '\n') == -1)
      if (verbosity) strerr_warnwu2sys("envdir ", fn) ;
  }
  if (wgola[GOLA_ENVDUMPDIR] && !env_dump4(wgola[GOLA_ENVDUMPDIR], 0700, envp, 0))
    if (verbosity) strerr_warnwu2sys("dump kernel environment to ", wgola[GOLA_ENVDUMPDIR]) ;

  if (!(wgolb & GOLB_NOLOGGER))
  {
    int fdr = open_read(LOGFIFO) ;
    if (fdr == -1) strerr_diefu1sys(111, "open " LOGFIFO) ;
    fd_close(1) ;
    if (open2(LOGFIFO, O_WRONLY) != 1) strerr_diefu1sys(111, "open " LOGFIFO) ;
    fd_close(fdr) ;
  }

  {
    pid_t pid ;
    char const *newenvp[2] = { 0, 0 } ;
    size_t pathlen = wgola[GOLA_PATH] ? strlen(wgola[GOLA_PATH]) : 0 ;
    char fmtfd[2 + UINT_FMT] = "-" ;
    char const *newargv[5] = { S6_EXTBINPREFIX "s6-svscan", fmtfd, "--", SCANDIRFULL, 0 } ;
    char pathvar[6 + pathlen] ;
    if (wgola[GOLA_PATH])
    {
      if (setenv("PATH", wgola[GOLA_PATH], 1) == -1)
        strerr_diefu1sys(111, "set initial PATH") ;
      memcpy(pathvar, "PATH=", 5) ;
      memcpy(pathvar + 5, wgola[GOLA_PATH], pathlen + 1) ;
      newenvp[0] = pathvar ;
    }
    if (wgolb & GOLB_NOLOGGER && pipe(notifpipe) < 0) strerr_diefu1sys(111, "pipe") ;
    if (tty && !wgola[GOLA_SLASHDEV] && ioctl(1 + !(wgolb & GOLB_NOLOGGER), TIOCNOTTY) == -1)
      if (verbosity) strerr_warnwu1sys("relinquish control terminal") ;

    pid = fork() ;
    if (pid == -1) strerr_diefu1sys(111, "fork") ;
    if (!pid) run_stage2(wgola[GOLA_BASEDIR], argv, argc, newenvp, !!wgola[GOLA_PATH], envmodifs.s, envmodifs.len, wgola[GOLA_INITDEFAULT], tty, wgolb) ;

    reset_stdin() ;
    setsid() ; /* just in case our caller is something weird */
    if (wgolb & GOLB_NOLOGGER)
    {
      close(notifpipe[0]) ;
      fmtfd[1] = 'd' ;
      fmtfd[2 + uint_fmt(fmtfd + 2, notifpipe[1])] = 0 ;
      if (!(wgolb & GOLB_INNS)) kbspecials() ;
    }
    else
    {
      int fd = dup(2) ;
      if (fd < 0) strerr_diefu1sys(111, "dup stderr") ;
      fmtfd[1] = 'X' ;
      fmtfd[2 + uint_fmt(fmtfd + 2, (unsigned int)fd)] = 0 ;
      if (!(wgolb & GOLB_INNS)) kbspecials() ;
      if (fd_copy(2, 1) == -1)
        strerr_diefu1sys(111, "redirect output file descriptor") ;
    }
    xmexec_fm(newargv, newenvp, !!wgola[GOLA_PATH], envmodifs.s, envmodifs.len) ;
  }
}
