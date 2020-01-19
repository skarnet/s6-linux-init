/* ISC license. */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

#include <skalibs/posixplz.h>
#include <skalibs/uint32.h>
#include <skalibs/types.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/bytestr.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/sig.h>
#include <skalibs/tai.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>
#include <skalibs/iopause.h>
#include <skalibs/skamisc.h>

#include <execline/config.h>

#include <s6/config.h>
#include <s6/s6-supervise.h>

#include <s6-linux-init/config.h>
#include "defaults.h"
#include "initctl.h"
#include "hpr.h"

#define STAGE4_FILE "stage 4"
#define SCANDIRFULL S6_LINUX_INIT_TMPFS "/" SCANDIR
#define SCANPREFIX SCANDIRFULL "/"
#define SCANPREFIXLEN (sizeof(SCANPREFIX) - 1)
#define DOTPREFIX ".s6-linux-init-shutdownd:"
#define DOTPREFIXLEN (sizeof(DOTPREFIX) - 1)
#define DOTSUFFIX ":XXXXXX"
#define DOTSUFFIXLEN (sizeof(DOTSUFFIX) - 1)

#define USAGE "s6-linux-init-shutdownd [ -c basedir ] [ -g gracetime ] [ -C ] [ -B ]"
#define dieusage() strerr_dieusage(100, USAGE)

static char const *basedir = BASEDIR ;
static int inns = 0 ;
static int nologger = 0 ;

struct at_s
{
  int fd ;
  char const *name ;
} ;

static int renametemp (char const *s, mode_t mode, void *data)
{
  struct at_s *at = data ;
  (void)mode ;
  return renameat(at->fd, at->name, at->fd, s) ;
}

static int mkrenametemp (int fd, char const *src, char *dst)
{
  struct at_s at = { .fd = fd, .name = src } ;
  return mkfiletemp(dst, &renametemp, 0700, &at) ;
}

static inline void run_stage3 (char const *basedir, char const *const *envp)
{
  pid_t pid ;
  size_t basedirlen = strlen(basedir) ;
  char stage3[basedirlen + sizeof("/scripts/" STAGE3)] ;
  char const *stage3_argv[2] = { stage3, 0 } ;
  memcpy(stage3, basedir, basedirlen) ;
  memcpy(stage3 + basedirlen, "/scripts/" STAGE3, sizeof("/scripts/" STAGE3)) ;
  pid = child_spawn0(stage3_argv[0], stage3_argv, envp) ;
  if (pid)
  {
    int wstat ;
    if (wait_pid(pid, &wstat) == -1) strerr_diefu1sys(111, "waitpid") ;
    if (WIFSIGNALED(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
      strerr_warnw3x(stage3, " was killed by signal ", fmt) ;
    }
    else if (WEXITSTATUS(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
      strerr_warnw3x(stage3, " exited ", fmt) ;
    }
  }
  else strerr_warnwu2sys("spawn ", stage3) ;
}

static inline void prepare_shutdown (buffer *b, tain_t *deadline, unsigned int *grace_time)
{
  uint32_t u ;
  char pack[TAIN_PACK + 4] ;
  ssize_t r = sanitize_read(buffer_get(b, pack, TAIN_PACK + 4)) ;
  if (r == -1) strerr_diefu1sys(111, "read from pipe") ;
  if (r < TAIN_PACK + 4) strerr_dief1x(101, "bad shutdown protocol") ;
  tain_unpack(pack, deadline) ;
  tain_add_g(deadline, deadline) ;
  uint32_unpack_big(pack + TAIN_PACK, &u) ;
  if (u && u <= 300000) *grace_time = u ;
}

static inline void handle_fifo (buffer *b, char *what, tain_t *deadline, unsigned int *grace_time)
{
  for (;;)
  {
    char c ;
    ssize_t r = sanitize_read(buffer_get(b, &c, 1)) ;
    if (r == -1) strerr_diefu1sys(111, "read from pipe") ;
    else if (!r) break ;
    switch (c)
    {
      case 'S' :
      case 'h' :
      case 'p' :
      case 'r' :
        *what = c ;
        prepare_shutdown(b, deadline, grace_time) ;
        break ;
      case 'c' :
        *what = 'S' ;
        tain_add_g(deadline, &tain_infinite_relative) ;
        break ;
      default :
      {
        char s[2] = { c, 0 } ;
        strerr_warnw2x("unknown command: ", s) ;
      }
      break ;
    }
  }
}

static void restore_console (void)
{
  if (!inns && !nologger)
  {
    fd_close(1) ;
    if (open("/dev/console", O_WRONLY) != 1)
      strerr_diefu1sys(111, "open /dev/console for writing") ;
    if (fd_copy(2, 1) < 0) strerr_warnwu1sys("fd_copy") ;
  }
}

static inline void prepare_stage4 (char const *basedir, char what)
{
  buffer b ;
  int fd ;
  char buf[512] ;
  size_t sabase = satmp.len ;
  unlink_void(STAGE4_FILE ".new") ;
  fd = open_excl(STAGE4_FILE ".new") ;
  if (fd == -1) strerr_diefu3sys(111, "open ", STAGE4_FILE ".new", " for writing") ;
  buffer_init(&b, &buffer_write, fd, buf, 512) ;
  if (inns)
  {
    if (buffer_puts(&b, "#!"
       EXECLINE_SHEBANGPREFIX "execlineb -P\n\n"
       EXECLINE_EXTBINPREFIX "foreground { "
       S6_EXTBINPREFIX "s6-svc -Ox -- . }\n"
       EXECLINE_EXTBINPREFIX "background\n{\n  ") < 0
     || (!nologger && buffer_puts(&b,
       EXECLINE_EXTBINPREFIX "foreground { "
       S6_EXTBINPREFIX "s6-svc -Xh -- " SCANPREFIX LOGGER_SERVICEDIR " }\n  ") < 0)
     || buffer_puts(&b, S6_EXTBINPREFIX "s6-svscanctl -") < 0
     || buffer_put(&b, what == 'h' ? "s" : &what, 1) < 0
     || buffer_putsflush(&b, "b -- " SCANDIRFULL "\n}\n") < 0)
      strerr_diefu2sys(111, "write to ", STAGE4_FILE ".new") ;
  }
  else
  {
    if (buffer_puts(&b, "#!"
      EXECLINE_SHEBANGPREFIX "execlineb -P\n\n"
      EXECLINE_EXTBINPREFIX "foreground { "
      S6_LINUX_INIT_BINPREFIX "s6-linux-init-umountall }\n"
      EXECLINE_EXTBINPREFIX "foreground { ") < 0
     || !string_quote(&satmp, basedir, strlen(basedir))
     || buffer_put(&b, satmp.s + sabase, satmp.len - sabase) < 0
     || buffer_puts(&b, "/scripts/" STAGE4 " }\n"
      S6_LINUX_INIT_BINPREFIX "s6-linux-init-hpr -f -") < 0
     || buffer_put(&b, &what, 1) < 0
     || buffer_putsflush(&b, "\n") < 0)
      strerr_diefu2sys(111, "write to ", STAGE4_FILE ".new") ;
    satmp.len = sabase ;
  }
  if (fchmod(fd, S_IRWXU) == -1)
    strerr_diefu2sys(111, "fchmod ", STAGE4_FILE ".new") ;
  fd_close(fd) ;
  if (rename(STAGE4_FILE ".new", STAGE4_FILE) == -1)
    strerr_diefu4sys(111, "rename ", STAGE4_FILE ".new", " to ", STAGE4_FILE) ;
}

static inline void unsupervise_tree (void)
{
  char const *except[3] =
  {
    SHUTDOWND_SERVICEDIR,
    nologger ? 0 : LOGGER_SERVICEDIR,
    0
  } ;
  DIR *dir = opendir(SCANDIRFULL) ;
  int fdd ;
  if (!dir) strerr_diefu1sys(111, "opendir " SCANDIRFULL) ;
  fdd = dirfd(dir) ;
  if (fdd == -1)
    strerr_diefu1sys(111, "dir_fd " SCANDIRFULL) ;
  for (;;)
  {
    char const *const *p = except ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    for (; *p ; p++) if (!strcmp(*p, d->d_name)) break ;
    if (!*p)
    {
      size_t dlen = strlen(d->d_name) ;
      char fn[SCANPREFIXLEN + DOTPREFIXLEN + dlen + DOTSUFFIXLEN + 1] ;
      memcpy(fn, SCANPREFIX DOTPREFIX, SCANPREFIXLEN + DOTPREFIXLEN) ;
      memcpy(fn + SCANPREFIXLEN + DOTPREFIXLEN, d->d_name, dlen) ;
      memcpy(fn + SCANPREFIXLEN + DOTPREFIXLEN + dlen, DOTSUFFIX, DOTSUFFIXLEN + 1) ;
      if (mkrenametemp(fdd, d->d_name, fn + SCANPREFIXLEN) == -1)
      {
        strerr_warnwu4sys("rename " SCANPREFIX, d->d_name, " to something based on ", fn) ;
        unlinkat(fdd, d->d_name, 0) ;
        /* if it still fails, too bad, it will restart in stage 4 and race */
      }
      else
        s6_svc_writectl(fn, S6_SUPERVISE_CTLDIR, "dx", 2) ;
    }
  }
  if (errno)
    strerr_diefu1sys(111, "readdir " SCANDIRFULL) ;
  dir_close(dir) ;
}

int main (int argc, char const *const *argv, char const *const *envp)
{
  unsigned int grace_time = 3000 ;
  tain_t deadline ;
  int fdr, fdw ;
  buffer b ;
  char what = 'S' ;
  char buf[64] ;
  PROG = "s6-linux-init-shutdownd" ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "c:g:CB", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : basedir = l.arg ; break ;
        case 'g' : if (!uint0_scan(l.arg, &grace_time)) dieusage() ; break ;
        case 'C' : inns = 1 ; break ;
	case 'B' : nologger = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (basedir[0] != '/')
    strerr_dief2x(100, "basedir", " must be an absolute path") ;
  if (grace_time > 300000) grace_time = 300000 ;

 /* if we're in stage 4, exec it immediately */
  {
    char const *stage4_argv[2] = { "./" STAGE4_FILE, 0 } ;
    restore_console() ;
    execve(stage4_argv[0], (char **)stage4_argv, (char *const *)envp) ;
    if (errno != ENOENT)
      strerr_warnwu2sys("exec ", stage4_argv[0]) ;
  }

  fdr = open_read(SHUTDOWND_FIFO) ;
  if (fdr == -1 || coe(fdr) == -1)
    strerr_diefu3sys(111, "open ", SHUTDOWND_FIFO, " for reading") ;
  fdw = open_write(SHUTDOWND_FIFO) ;
  if (fdw == -1 || coe(fdw) == -1)
    strerr_diefu3sys(111, "open ", SHUTDOWND_FIFO, " for writing") ;
  if (sig_ignore(SIGPIPE) == -1)
    strerr_diefu1sys(111, "sig_ignore SIGPIPE") ;
  buffer_init(&b, &buffer_read, fdr, buf, 64) ;
  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &tain_infinite_relative) ;

  for (;;)
  {
    iopause_fd x = { .fd = fdr, .events = IOPAUSE_READ } ;
    int r = iopause_g(&x, 1, &deadline) ;
    if (r == -1) strerr_diefu1sys(111, "iopause") ;
    if (!r)
    {
      run_stage3(basedir, envp) ;
      tain_now_g() ;
      if (what != 'S') break ;
      tain_add_g(&deadline, &tain_infinite_relative) ;
      continue ;
    }
    if (x.revents & IOPAUSE_READ)
      handle_fifo(&b, &what, &deadline, &grace_time) ;
  }

  fd_close(fdw) ;
  fd_close(fdr) ;
  restore_console() ;


 /* The end is coming! */

  prepare_stage4(basedir, what) ;
  unsupervise_tree() ;
  if (sig_ignore(SIGTERM) == -1) strerr_warnwu1sys("sig_ignore SIGTERM") ;
  if (!inns)
  {
    sync() ;
    strerr_warni1x("sending all processes the TERM signal...") ;
  }
  kill(-1, SIGTERM) ;
  kill(-1, SIGCONT) ;
  tain_from_millisecs(&deadline, grace_time) ;
  tain_now_g() ;
  tain_add_g(&deadline, &deadline) ;
  deepsleepuntil_g(&deadline) ;
  if (!inns)
  {
    sync() ;
    strerr_warni1x("sending all processes the KILL signal...") ;
  }
  kill(-1, SIGKILL) ;
  return 0 ;
}
