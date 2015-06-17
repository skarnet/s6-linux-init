/* ISC license. */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <skalibs/config.h>
#include <skalibs/uint64.h>
#include <skalibs/uint.h>
#include <skalibs/gidstuff.h>
#include <skalibs/bytestr.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/sgetopt.h>
#include <skalibs/skamisc.h>
#include <execline/config.h>
#include <s6/config.h>

#define USAGE "s6-linux-init-maker [ -c basedir ] [ -l tmpfsdir ] [ -b execline_bindir ] [ -u log_user ] [ -g early_getty_cmd ] [ -2 stage2_script ] [ -r ] [ -3 stage3_script ] [ -p initial_path ] [ -m initial_umask ] [ -t timestamp_style ] [ -d dev_style ] [ -e initial_envvar ... ] dir"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb") ;

#define BANNER "*\n* init created by s6-linux-init-maker\n* see http://skarnet.org/software/s6-linux-init/\n*\n"

#define CRASH_SCRIPT \
"#!" EXECLINE_EXTBINPREFIX "execlineb -P\n\n" \
"redirfd -r 0 /dev/console\n" \
"redirfd -w 1 /dev/console\n" \
"fdmove -c 2 1\n" \
"foreground { s6-echo -- " \
"\"s6-svscan crashed. Dropping to an interactive shell.\" }\n" \
"/bin/sh -i\n"

static char const *slashrun = "/run" ;
static char const *robase = "/etc/s6-linux-init" ;
static char const *init_script = "/etc/rc.init" ;
static char const *shutdown_script = "/etc/rc.shutdown" ;
static char const *bindir = "/bin" ;
static char const *initial_path = SKALIBS_DEFAULTPATH ;
static char const *early_getty = 0 ;
static uid_t uncaught_logs_uid = 65534 ;
static gid_t uncaught_logs_gid = 65534 ;
static unsigned int initial_umask = 022 ;
static unsigned int timestamp_style = 1 ;
static unsigned int slashdev_style = 2 ;
static int redirect_stage2 = 0 ;

typedef int writetobuf_func_t (buffer *) ;
typedef writetobuf_func_t *writetobuf_func_t_ref ;

static int early_getty_script (buffer *b)
{
  if (buffer_puts(b, "#!" EXECLINE_EXTBINPREFIX "execlineb -P\n\n") < 0
   || buffer_puts(b, early_getty) < 0
   || buffer_put(b, "\n", 1) < 0)
    return 0 ;
  return 1 ;
}

static int s6_svscan_log_script (buffer *b)
{
  unsigned int sabase = satmp.len ;
  char fmt[UINT64_FMT] ;
  if (buffer_puts(b,
    "#!" EXECLINE_EXTBINPREFIX "execlineb -P\n\n"
    "redirfd -rnb 0 fifo\n"
    "s6-applyuidgid -u ") < 0
   || buffer_put(b, fmt, uint64_fmt(fmt, uncaught_logs_uid)) < 0
   || buffer_puts(b, " -g ") < 0
   || buffer_put(b, fmt, gid_fmt(fmt, uncaught_logs_gid)) < 0
   || buffer_puts(b, " --\ns6-log -bp -- ") < 0
   || buffer_puts(b, timestamp_style & 1 ? "t " : "") < 0
   || buffer_puts(b, timestamp_style & 2 ? "T " : "") < 0) return 0 ;
  if (!string_quote(&satmp, slashrun, str_len(slashrun))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0)
  {
    satmp.len = sabase ;
    return 0 ;
  }
  satmp.len = sabase ;
  if (buffer_puts(b, "/uncaught-logs\n") < 0) return 0 ;
  return 1 ;
}

static int finish_script (buffer *b)
{
  unsigned int sabase = satmp.len ;
  if (buffer_puts(b, 
    "#!" EXECLINE_EXTBINPREFIX "execlineb -S0\n\n"
    "cd /\nredirfd -w 2 /dev/console\nfdmove -c 1 2\nwait { }\n") < 0
   || !string_quote(&satmp, shutdown_script, str_len(shutdown_script))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0)
  {
    satmp.len = sabase ;
    return 0 ;
  }
  satmp.len = sabase ;
  if (buffer_puts(b, " ${@}\n") < 0) return 0 ;
  return 1 ;
}

static void cleanup (char const *base)
{
  int e = errno ;
  rm_rf(base) ;
  errno = e ;
}

static void auto_dir (char const *base, char const *dir, uid_t uid, gid_t gid, unsigned int mode)
{
  unsigned int clen = str_len(base) ;
  unsigned int dlen = str_len(dir) ;
  char fn[clen + dlen + 2] ;
  byte_copy(fn, clen, base) ;
  fn[clen] = dlen ? '/' : 0 ;
  byte_copy(fn + clen + 1, dlen + 1, dir) ;
  if (mkdir(fn, mode) < 0
   || ((uid || gid) && (chown(fn, uid, gid) < 0 || chmod(fn, mode) < 0)))
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "mkdir ", fn) ;
  }
}

static void auto_file (char const *base, char const *file, char const *s, unsigned int n, int executable)
{
  unsigned int clen = str_len(base) ;
  unsigned int flen = str_len(file) ;
  char fn[clen + flen + 2] ;
  byte_copy(fn, clen, base) ;
  fn[clen] = '/' ;
  byte_copy(fn + clen + 1, flen + 1, file) ;
  if (!openwritenclose_unsafe(fn, s, n)
   || (executable && chmod(fn, 0755) < 0))
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
}

static void auto_fifo (char const *base, char const *fifo)
{
  unsigned int baselen = str_len(base) ;
  unsigned int fifolen = str_len(fifo) ;
  char fn[baselen + fifolen + 2] ;
  byte_copy(fn, baselen, base) ;
  fn[baselen] = '/' ;
  byte_copy(fn + baselen + 1, fifolen + 1, fifo) ;
  if (mkfifo(fn, 0600) < 0)
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "mkfifo ", fn) ;
  }
}

static void auto_script (char const *base, char const *file, writetobuf_func_t_ref scriptf)
{
  char buf[4096] ;
  buffer b ;
  int fd ;
  unsigned int baselen = str_len(base) ;
  unsigned int filelen = str_len(file) ;
  char fn[baselen + filelen + 2] ;
  byte_copy(fn, baselen, base) ;
  fn[baselen] = '/' ;
  byte_copy(fn + baselen + 1, filelen + 1, file) ;
  fd = open_trunc(fn) ;
  if (fd < 0 || ndelay_off(fd) < 0 || fchmod(fd, 0755) < 0)
  {
    cleanup(base) ;
    strerr_diefu3sys(111, "open ", fn, " for script writing") ;
  }
  buffer_init(&b, &fd_writesv, fd, buf, 4096) ;
  if (!(*scriptf)(&b) || !buffer_flush(&b))
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
  close(fd) ;
}

static void make_image (char const *base)
{
  auto_dir(base, "run-image", 0, 0, 0755) ;
  auto_dir(base, "run-image/uncaught-logs", uncaught_logs_uid, uncaught_logs_gid, 02700) ;
  auto_dir(base, "run-image/service", 0, 0, 0755) ;
  auto_dir(base, "run-image/service/.s6-svscan", 0, 0, 0755) ;
  auto_file(base, "run-image/service/.s6-svscan/crash", CRASH_SCRIPT, sizeof(CRASH_SCRIPT) - 1, 1) ;
  auto_script(base, "run-image/service/.s6-svscan/finish", &finish_script) ;
  auto_dir(base, "run-image/service/s6-svscan-log", 0, 0, 0755) ;
  auto_fifo(base, "run-image/service/s6-svscan-log/fifo") ;
  auto_script(base, "run-image/service/s6-svscan-log/run", &s6_svscan_log_script) ;
  if (early_getty)
  {
    auto_dir(base, "run-image/service/s6-linux-init-early-getty", 0, 0, 0755) ;
    auto_script(base, "run-image/service/s6-linux-init-early-getty/run", &early_getty_script) ;
  }
}

static void make_env (char const *base, char const *modif, unsigned int modiflen)
{
  auto_dir(base, "env", 0, 0, 0755) ;
  while (modiflen)
  {
    unsigned int len = str_len(modif) ;
    unsigned int pos = byte_chr(modif, len, '=') ;
    char fn[5 + pos] ;
    byte_copy(fn, 4, "env/") ;
    byte_copy(fn + 4, pos, modif) ;
    fn[4 + pos] = 0 ;
    
    if (pos + 1 < len) auto_file(base, fn, modif + pos + 1, len - pos - 1, 0) ;
    else if (pos + 1 == len) auto_file(base, fn, "\n", 1, 0) ;
    else auto_file(base, fn, "", 0, 0) ;
    modif += len+1 ; modiflen -= len+1 ;
  }
}

static int make_init_script (buffer *b)
{
  unsigned int sabase = satmp.len, pos, pos2 ;
  char fmt[UINT_OFMT] ;
  if (buffer_puts(b, "#!") < 0
   || buffer_puts(b, bindir) < 0
   || buffer_puts(b, "/execlineb -P\n\n") < 0
   || buffer_puts(b, bindir) < 0
   || buffer_puts(b, "/export PATH ") < 0
   || !string_quote(&satmp, initial_path, str_len(initial_path))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
  satmp.len = sabase ;
  if (buffer_put(b, "\n", 1) < 0
   || buffer_puts(b, bindir) < 0
   || buffer_puts(b, "/cd /\numask 0") < 0
   || buffer_put(b, fmt, uint_ofmt(fmt, initial_umask)) < 0
   || buffer_puts(b, "\nif { s6-echo -- ") < 0
   || !string_quote(&satmp, BANNER, sizeof(BANNER) - 1) < 0) return 0 ;
  if (buffer_put(b, satmp.s, satmp.len) < 0) goto err ;
  satmp.len = sabase ;
  if (buffer_puts(b, " }\nif { s6-mount -nwt tmpfs -o mode=0755 tmpfs ") < 0
   || !string_quote(&satmp, slashrun, str_len(slashrun))) return 0 ;
  pos = satmp.len ;
  if (buffer_put(b, satmp.s + sabase, pos - sabase) < 0
   || buffer_puts(b, " }\nif { s6-hiercopy ") < 0
   || !string_quote(&satmp, robase, str_len(robase))) return 0 ;
  pos2 = satmp.len ;
  if (buffer_put(b, satmp.s + pos, pos2 - pos) < 0
   || buffer_puts(b, "/run-image ") < 0
   || buffer_put(b, satmp.s + sabase, pos - sabase) < 0
   || buffer_puts(b, " }\n") < 0) goto err ;
  if (slashdev_style == 1)
  {
    if (buffer_puts(b, "if { s6-mount -nt devtmpfs dev /dev }\n") < 0) goto err ;
  }
  if (buffer_puts(b, "redirfd -r 0 /dev/null\ns6-envdir -I -- ") < 0
   || buffer_put(b, satmp.s + pos, pos2 - pos) < 0
   || buffer_puts(b, "/env\nbackground\n{\n  s6-setsid --\n  ") < 0) goto err ;
  if (redirect_stage2)
  {
    if (buffer_puts(b, "redirfd -w 2 ") < 0
     || buffer_put(b, satmp.s + sabase, pos - sabase) < 0
     || buffer_puts(b, "/service/s6-svscan-log/fifo\n  fdmove -c 1 2\n  ") < 0) goto err ;
  }
  else
  {
    if (buffer_puts(b, "redirfd -w 3 ") < 0
     || buffer_put(b, satmp.s + sabase, pos - sabase) < 0
     || buffer_puts(b, "/service/s6-svscan-log/fifo\n  fdclose 3\n  ") < 0) goto err ;
  }
  if (!string_quote(&satmp, init_script, str_len(init_script))
   || buffer_put(b, satmp.s + pos2, satmp.len - pos2) < 0
   || buffer_puts(b, "\n}\nunexport !\nredirfd -wnb 1 ") < 0
   || buffer_put(b, satmp.s + sabase, pos - sabase) < 0
   || buffer_puts(b, "/service/s6-svscan-log/fifo\nfdmove -c 2 1\ncd ") < 0
   || buffer_put(b, satmp.s + sabase, pos - sabase) < 0
   || buffer_puts(b, "/service\ns6-svscan -t0\n") < 0) goto err ;
  return 1 ;
 err:
  satmp.len = sabase ;
  return 0 ;
}

int main (int argc, char const *const *argv)
{
  char const *catchall_user = "nobody" ;
  stralloc modif = STRALLOC_ZERO ;
  PROG = "s6-linux-init-maker" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "c:l:b:u:g:2:r3:p:m:t:d:e:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : robase = l.arg ; break ;
        case 'l' : slashrun = l.arg ; break ;
        case 'b' : bindir = l.arg ; break ;
        case 'u' : catchall_user = l.arg ; break ;
        case 'g' : early_getty = l.arg ; break ;
        case '2' : init_script = l.arg ; break ;
        case 'r' : redirect_stage2 = 1 ; break ;
        case '3' : shutdown_script = l.arg ; break ;
        case 'p' : initial_path = l.arg ; break ;
        case 'm' : if (!uint0_oscan(l.arg, &initial_umask)) dieusage() ; break ;
        case 't' : if (!uint0_scan(l.arg, &timestamp_style)) dieusage() ; break ;
        case 'd' : if (!uint0_scan(l.arg, &slashdev_style)) dieusage() ; break ;
        case 'e' : if (!stralloc_catb(&modif, l.arg, str_len(l.arg) + 1)) dienomem() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (!argc) dieusage() ;

  if (robase[0] != '/')
    strerr_dief3x(100, "base directory ", robase, " is not absolute") ;
  if (slashrun[0] != '/')
    strerr_dief3x(100, "tmpfs directory ", slashrun, " is not absolute") ;
  if (bindir[0] != '/')
    strerr_dief3x(100, "initial location for binaries ", bindir, " is not absolute") ;
  if (init_script[0] != '/')
    strerr_dief3x(100, "stage 2 script location ", init_script, " is not absolute") ;
  if (shutdown_script[0] != '/')
    strerr_dief3x(100, "stage 3 script location ", shutdown_script, " is not absolute") ;
  if (timestamp_style > 3)
    strerr_dief1x(100, "-t timestamp_style must be 0, 1, 2 or 3") ;
  if (slashdev_style > 2)
    strerr_dief1x(100, "-d dev_style must be 0, 1 or 2") ;


  {
    struct passwd *pw = getpwnam(catchall_user) ;
    if (!pw)
    {
      if (errno)
        strerr_diefu2sys(111, "getpwnam for ", catchall_user) ;
      else
        strerr_dief3x(100, "getpwnam for ", catchall_user, ": no such user") ;
    }
    uncaught_logs_uid = pw->pw_uid ;
    uncaught_logs_gid = pw->pw_gid ;
  }

  if (mkdir(argv[0], 0755) < 0)
    strerr_diefu2sys(111, "mkdir ", argv[0]) ;

  make_env(argv[0], modif.s, modif.len) ;
  stralloc_free(&modif) ;
  make_image(argv[0]) ;
  if (!make_init_script(buffer_1) || !buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
  return 0 ;
}
