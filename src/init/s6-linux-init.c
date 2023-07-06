/* ISC license. */

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/ioctl.h>

#include <linux/kd.h>

#include <skalibs/types.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/sig.h>
#include <skalibs/env.h>
#include <skalibs/djbunix.h>
#include <skalibs/exec.h>

#include <s6/config.h>

#include <s6-linux-init/config.h>
#include "defaults.h"
#include "initctl.h"

#define USAGE "s6-linux-init [ -v verbosity ] [ -c basedir ] [ -p initpath ] [ -s envdumpdir ] [ -m umask ] [ -d devtmpfs ] [ -D initdefault ] [ -n | -N ] [ -C ] [ -B ]"
#define dieusage() strerr_dieusage(100, USAGE)

#define BANNER "\n  s6-linux-init version " S6_LINUX_INIT_VERSION "\n\n"

static int inns = 0 ;
static int nologger = 0 ;
static int notifpipe[2] ;
static unsigned int verbosity = 1 ;

static inline char const *scan_cmdline (char const *initdefault, char const *const *argv, unsigned int argc)
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
  int fd ;
  if (inns) return ;
  fd = open2("/dev/tty0", O_RDONLY | O_NOCTTY) ;
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

static inline void run_stage2 (char const *basedir, char const **argv, unsigned int argc, char const *const *envp, size_t envlen, char const *modifs, size_t modiflen, char const *initdefault, char const *tty)
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
  childargv[1] = scan_cmdline(initdefault, argv, argc) ;
  for (unsigned int i = 0 ; i < argc ; i++)
    childargv[i+2] = argv[i] ;
  childargv[argc + 2] = 0 ;
  if (nologger)
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
  mode_t mask = 0022 ;
  char const *basedir = BASEDIR ;
  char const *path = INITPATH ;
  char const *slashdev = 0 ;
  char const *envdumpdir = 0 ;
  char const *initdefault = "default" ;
  int mounttype = 1 ;
  int hasconsole = 1 ;
  char *tty = 0 ;
  stralloc envmodifs = STRALLOC_ZERO ;
  PROG = "s6-linux-init" ;

  if (getpid() != 1)
  {
    argv[0] = S6_LINUX_INIT_BINPREFIX "s6-linux-init-telinit" ;
    xexec_e(argv, envp) ;
  }

  {
    subgetopt l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:c:p:s:m:d:D:nNCB", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 'c' : basedir = l.arg ; break ;
        case 'p' : path = l.arg ; break ;
        case 's' : envdumpdir = l.arg ; break ;
        case 'm' : if (!uint0_oscan(l.arg, &mask)) dieusage() ; break ;
        case 'd' : slashdev = l.arg ; break ;
        case 'D' : initdefault = l.arg ; break ;
        case 'n' : mounttype = 2 ; break ;
        case 'N' : mounttype = 0 ; break ;
        case 'C' : inns = 1 ; break ;
        case 'B' : nologger = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (fcntl(1, F_GETFD) < 0) hasconsole = 0 ;
  if (inns)
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
    if (!slashdev && hasconsole && isatty(2 - nologger))
    {
      tty = ttyname(2 - nologger) ;
      if (!tty) if (verbosity) strerr_warnwu2sys("ttyname std", nologger ? "err" : "out") ;
    }
  }
  else if (hasconsole) allwrite(1, BANNER, sizeof(BANNER) - 1) ;
  if (chdir("/") == -1) strerr_diefu1sys(111, "chdir to /") ;
  umask(mask) ;

  if (slashdev)
  {
    int nope, e ;
    close(0) ;
    close(1) ;
    close(2) ;
   /* at this point we're totally in the dark, hoping /dev/console will work */
    nope = mount("dev", slashdev, "devtmpfs", MS_NOSUID | MS_NOEXEC, "") < 0 ;
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
    if (!slashdev) reset_stdin() ;
    if (open2("/dev/null", O_WRONLY) != 1 || fd_copy(2, 1) == -1)
      return 111 ;
  }

  if (mounttype)
  {
    if (mounttype == 2)
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
    size_t dirlen = strlen(basedir) ;
    char fn[dirlen + 1 + (sizeof(RUNIMAGE) > sizeof(ENVSTAGE1) ? sizeof(RUNIMAGE) : sizeof(ENVSTAGE1))] ;
    memcpy(fn, basedir, dirlen) ;
    fn[dirlen] = '/' ;
    memcpy(fn + dirlen + 1, RUNIMAGE, sizeof(RUNIMAGE)) ;
    if (!hiercopy_loose(fn, S6_LINUX_INIT_TMPFS))
      strerr_diefu3sys(111, "copy ", fn, " to " S6_LINUX_INIT_TMPFS) ;
    memcpy(fn + dirlen + 1, ENVSTAGE1, sizeof(ENVSTAGE1)) ;
    if (envdir_internal(fn, &envmodifs, SKALIBS_ENVDIR_VERBATIM | SKALIBS_ENVDIR_NOCLAMP, '\n') == -1)
      if (verbosity) strerr_warnwu2sys("envdir ", fn) ;
  }
  if (envdumpdir && !env_dump(envdumpdir, 0700, envp))
    if (verbosity) strerr_warnwu2sys("dump kernel environment to ", envdumpdir) ;

  if (!nologger)
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
    size_t pathlen = path ? strlen(path) : 0 ;
    char fmtfd[2 + UINT_FMT] = "-" ;
    char const *newargv[5] = { S6_EXTBINPREFIX "s6-svscan", fmtfd, "--", SCANDIRFULL, 0 } ;
    char pathvar[6 + pathlen] ;
    if (path)
    {
      if (setenv("PATH", path, 1) == -1)
        strerr_diefu1sys(111, "set initial PATH") ;
      memcpy(pathvar, "PATH=", 5) ;
      memcpy(pathvar + 5, path, pathlen + 1) ;
      newenvp[0] = pathvar ;
    }
    if (nologger && pipe(notifpipe) < 0) strerr_diefu1sys(111, "pipe") ;
    if (tty && !slashdev && ioctl(2 - nologger, TIOCNOTTY) == -1)
      if (verbosity) strerr_warnwu1sys("relinquish control terminal") ;

    pid = fork() ;
    if (pid == -1) strerr_diefu1sys(111, "fork") ;
    if (!pid) run_stage2(basedir, argv, argc, newenvp, !!path, envmodifs.s, envmodifs.len, initdefault, tty) ;

    reset_stdin() ;
    setsid() ; /* just in case our caller is something weird */
    if (nologger)
    {
      close(notifpipe[0]) ;
      fmtfd[1] = 'd' ;
      fmtfd[2 + uint_fmt(fmtfd + 2, notifpipe[1])] = 0 ;
      kbspecials() ;
    }
    else
    {
      int fd = dup(2) ;
      if (fd < 0) strerr_diefu1sys(111, "dup stderr") ;
      fmtfd[1] = 'X' ;
      fmtfd[2 + uint_fmt(fmtfd + 2, (unsigned int)fd)] = 0 ;
      kbspecials() ;
      if (fd_copy(2, 1) == -1)
        strerr_diefu1sys(111, "redirect output file descriptor") ;
    }
    xmexec_fm(newargv, newenvp, !!path, envmodifs.s, envmodifs.len) ;
  }
}
