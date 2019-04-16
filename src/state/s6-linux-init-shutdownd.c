/* ISC license. */

#include <sys/types.h>
#include <sys/stat.h>
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
#include <skalibs/stralloc.h>
#include <skalibs/sig.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/iopause.h>
#include <skalibs/skamisc.h>

#include "defaults.h"
#include "initctl.h"
#include "hpr.h"

#define STAGE4_FILE "stage 4"

#define USAGE "s6-linux-init-shutdownd [ -b bindir ] [ -c basedir ] [ -g gracetime ]"
#define dieusage() strerr_dieusage(100, USAGE)

static char const *basedir = BASEDIR ;

static inline void prepare_shutdown (char c, unsigned int *what, tain_t *deadline, unsigned int *grace_time)
{
  uint32_t u ;
  char pack[TAIN_PACK] ;
  ssize_t r = sanitize_read(buffer_get(buffer_0small, pack, TAIN_PACK)) ;
  if (r == -1) strerr_diefu1sys(111, "read from pipe") ;
  if (r < TAIN_PACK + 4) strerr_dief1x(101, "bad shutdown protocol") ;
  tain_unpack(pack, deadline) ;
  uint32_unpack_big(pack + TAIN_PACK, &u) ;
  if (u && u <= 300000) *grace_time = u ;
  *what = 1 + byte_chr("hpr", c, 3) ;
}

static inline void handle_stdin (unsigned int *what, tain_t *deadline, unsigned int *grace_time)
{
  for (;;)
  {
    char c ;
    ssize_t r = sanitize_read(buffer_get(buffer_0small, &c, 1)) ;
    if (r == -1) strerr_diefu1sys(111, "read from pipe") ;
    else if (!r) break ;
    switch (c)
    {
      case 'h' :
      case 'p' :
      case 'r' :
        prepare_shutdown(c, what, deadline, grace_time) ;
        break ;
      case 'c' :
        *what = 0 ;
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

static inline void prepare_stage4 (char const *bindir, char const *basedir, unsigned int what)
{
  static char const *table[3] = { "halt", "poweroff", "reboot" } ;
  stralloc sa = STRALLOC_ZERO ;
  buffer b ;
  char buf[512] ;
  int fd ;
  unlink_void(STAGE4_FILE ".new") ;
  fd = open_excl(STAGE4_FILE ".new") ;
  if (fd == -1) strerr_diefu3sys(111, "open ", STAGE4_FILE ".new", " for writing") ;
  buffer_init(&b, &buffer_write, fd, buf, 512) ;

  if (buffer_puts(&b, "#!") == -1
   || buffer_puts(&b, bindir) == -1
   || buffer_puts(&b, "/execlineb -P\n\n") == -1)
    strerr_diefu2sys(111, "write to ", STAGE4_FILE ".new") ;
  if (!string_quote(&sa, basedir, strlen(basedir)))
    strerr_diefu1sys(111, "string_quote") ;
  if (buffer_put(&b, sa.s, sa.len) == -1
   || buffer_puts(&b, "/bin/" STAGE4 " ") == -1
   || buffer_puts(&b, table[what-1]) == -1
   || buffer_putsflush(&b, "\n") == -1)
    strerr_diefu2sys(111, "write to ", STAGE4_FILE ".new") ;
  stralloc_free(&sa) ;
  if (fchmod(fd, S_IRWXU) == -1)
    strerr_diefu2sys(111, "fchmod ", STAGE4_FILE ".new") ;
  if (fsync(fd) == -1)
    strerr_diefu2sys(111, "fsync ", STAGE4_FILE ".new") ;
  fd_close(fd) ;
  if (rename(STAGE4_FILE ".new", STAGE4_FILE) == -1)
    strerr_diefu4sys(111, "rename ", STAGE4_FILE ".new", " to ", STAGE4_FILE) ;
}

int main (int argc, char const *const *argv)
{
  char const *bindir = BINDIR ;
  pid_t pid ;
  tain_t deadline ;
  unsigned int what = 0 ;
  unsigned int grace_time = 3000 ;
  int notif = 0 ;
  PROG = "s6-linux-init-shutdownd" ;

  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "b:c:g:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'b' : bindir = l.arg ; break ;
        case 'c' : basedir = l.arg ; break ;
        case 'g' : if (!uint0_scan(l.arg, &grace_time)) dieusage() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (bindir[0] != '/')
    strerr_dief2x(100, "bindir", " must be an absolute path") ;
  if (basedir[0] != '/')
    strerr_dief2x(100, "basedir", " must be an absolute path") ;
  if (grace_time > 300000) grace_time = 300000 ;

 /* if we're in stage 4, exec it immediately */
  {
    char const *stage4_argv[2] = { STAGE4_FILE, 0 } ;
    pathexec_run(stage4_argv[0], stage4_argv, (char const *const *)environ) ;
    if (errno != ENOENT)
      strerr_warnwu2sys("exec ", stage4_argv[0]) ;
  }

  fd_close(1) ;
  fd_close(0) ;

  if (open_read(SHUTDOWND_FIFO))
    strerr_diefu3sys(111, "open ", SHUTDOWND_FIFO, " for reading") ;
  if (open_write(SHUTDOWND_FIFO) != 1)
    strerr_diefu3sys(111, "open ", SHUTDOWND_FIFO, " for writing") ;
  if (sig_ignore(SIGPIPE) == -1)
    strerr_diefu1sys(111, "sig_ignore SIGPIPE") ;
  tain_now_g() ;
  tain_add_g(&deadline, &tain_infinite_relative) ;

  for (;;)
  {
    iopause_fd x = { .fd = 0, .events = IOPAUSE_READ } ;
    int r = iopause_g(&x, 1, &deadline) ;
    if (r == -1) strerr_diefu1sys(111, "iopause") ;
    if (!r)
    {
      if (what) break ;
      tain_add_g(&deadline, &tain_infinite_relative) ;
      continue ;
    }
    if (x.revents & IOPAUSE_READ)
      handle_stdin(&what, &deadline, &grace_time) ;
  }


 /* Stage 3 */

  fd_close(1) ;
  fd_close(0) ;
  if (open_readb("/dev/null"))
    strerr_diefu1sys(111, "open /dev/null for reading") ;
  if (open_write("/dev/console") != 1 || ndelay_off(1) == -1)
    strerr_diefu1sys(111, "open /dev/console for writing") ;
  {
    size_t basedirlen = strlen(basedir) ;
    char stage3[basedirlen + sizeof("/" STAGE3)] ;
    char const *stage3_argv[2] = { stage3, 0 } ;
    memcpy(stage3, basedir, basedirlen) ;
    memcpy(stage3 + basedirlen, "/" STAGE3, sizeof("/" STAGE3)) ;
    sig_block(SIGCHLD) ;
    pid = child_spawn0(stage3_argv[0], stage3_argv, (char const *const *)environ) ;
    if (!pid) strerr_warnwu2sys("spawn ", stage3) ;
    sig_unblock(SIGCHLD) ;
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
  }


 /* The end is coming! */

  prepare_stage4(bindir, basedir, what) ;
  if (fd_move(2, 1) == -1) strerr_warnwu1sys("fd_move") ;
  sync() ;
  if (sig_ignore(SIGTERM) == -1) strerr_warnwu1sys("sig_ignore SIGTERM") ;
  strerr_warni1x("sending all processes the TERM signal...") ;
  kill(-1, SIGTERM) ;
  kill(-1, SIGCONT) ;
  tain_from_millisecs(&deadline, grace_time) ;
  tain_add_g(&deadline, &deadline) ;
  deepsleepuntil_g(&deadline) ;
  sync() ;
  strerr_warni1x("sending all processes the KILL signal...") ;
  kill(-1, SIGKILL) ;
  return 0 ;
}
