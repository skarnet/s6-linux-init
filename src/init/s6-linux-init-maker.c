/* ISC license. */

#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/bytestr.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/env.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/sgetopt.h>
#include <skalibs/skamisc.h>

#include <execline/config.h>

#include <s6/config.h>

#include <s6-linux-init/config.h>
#include "defaults.h"
#include "initctl.h"

#ifdef S6_LINUX_INIT_UTMPD_PATH
# include <utmps/config.h>
# define USAGE "s6-linux-init-maker [ -c basedir ] [ -u log_user ] [ -G early_getty_cmd ] [ -1 ] [ -L ] [ -p initial_path ] [ -m initial_umask ] [ -t timestamp_style ] [ -d slashdev ] [ -s env_store ] [ -e initial_envvar ... ] [ -q default_grace_time ] [ -D initdefault ] [ -n | -N ] [ -f skeldir ] [ -U utmp_user ] [ -C ] [ -B ] dir"
# define OPTION_STRING "c:u:G:1Lp:m:t:d:s:e:E:q:D:nNf:U:CB"
# define UTMPS_DIR "utmps"
#else
# define USAGE "s6-linux-init-maker [ -c basedir ] [ -u log_user ] [ -G early_getty_cmd ] [ -1 ] [ -L ] [ -p initial_path ] [ -m initial_umask ] [ -t timestamp_style ] [ -d slashdev ] [ -s env_store ] [ -e initial_envvar ... ] [ -q default_grace_time ] [ -D initdefault ] [ -n | -N ] [ -f skeldir ] [ -C ] [ -B ] dir"
# define OPTION_STRING "c:u:G:1Lp:m:t:d:s:e:E:q:D:nNf:CB"
#endif

#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb") ;

#define UNCAUGHT_DIR "uncaught-logs"

static char const *robase = BASEDIR ;
static char const *initial_path = INITPATH ;
static char const *env_store = 0 ;
static char const *early_getty = 0 ;
static char const *slashdev = 0 ;
static char const *log_user = "root" ;
static char const *initdefault = 0 ;
static char const *skeldir = S6_LINUX_INIT_SKELDIR ;
static unsigned int initial_umask = 0022 ;
static unsigned int timestamp_style = 1 ;
static unsigned int finalsleep = 3000 ;
static int mounttype = 1 ;
static int console = 0 ;
static int logouthookd = 0 ;
static int inns = 0 ;
static int nologger = 0 ;

#ifdef S6_LINUX_INIT_UTMPD_PATH
static char const *utmp_user = "" ;
#endif

typedef int writetobuf_func_t (buffer *, char const *) ;
typedef writetobuf_func_t *writetobuf_func_t_ref ;

#define put_shebang(b) put_shebang_options((b), 0)

static int put_shebang_options (buffer *b, char const *options)
{
  return buffer_puts(b, "#!" EXECLINE_SHEBANGPREFIX "execlineb ") >= 0
   && buffer_puts(b, options && options[0] ? options : "-P") >= 0
   && buffer_puts(b, "\n\n") >= 0 ;
}

static int line_script (buffer *b, char const *line)
{
  return put_shebang(b)
   && buffer_puts(b, line) >= 0
   && buffer_put(b, "\n", 1) >= 0 ;
}

static int linewithargs_script (buffer *b, char const *line)
{
  return put_shebang_options(b, "-S0")
   && buffer_puts(b, line) >= 0
   && buffer_puts(b, " $@\n") >= 0 ;
}

static int hpr_script (buffer *b, char const *what)
{
  if (!put_shebang_options(b, "-S0")
   || buffer_puts(b, S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-hpr -") < 0
   || (inns && buffer_puts(b, "n") < 0)
   || buffer_puts(b, what) < 0
   || buffer_puts(b, " $@\n") < 0) return 0 ;
  return 1 ;
}

static int death_script (buffer *b, char const *s)
{
  return put_shebang(b)
   && buffer_puts(b,
     EXECLINE_EXTBINPREFIX "redirfd -w 2 /dev/console\n"
     EXECLINE_EXTBINPREFIX "fdmove -c 1 2\n"
     EXECLINE_EXTBINPREFIX "foreground { "
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-echo -- \"s6-svscan ") >= 0
   && buffer_puts(b, s) >= 0
   && buffer_puts(b,
     ". Rebooting.\" }\n"
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-hpr -fr\n") >= 0 ;
}

static int container_crash_script (buffer *b, char const *data)
{
  (void)data ;
  return put_shebang(b)
   && buffer_puts(b,
     EXECLINE_EXTBINPREFIX "foreground\n{\n  "
     EXECLINE_EXTBINPREFIX "fdmove -c 1 2\n  "
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-echo -- \"s6-svscan crashed. Killing everything and exiting.\"\n}\n"
     EXECLINE_EXTBINPREFIX "foreground { "
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-nuke }\n"
     EXECLINE_EXTBINPREFIX "wait { }\n"
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-hpr -fnp\n") >= 0 ;
}

static int container_exit_script (buffer *b, char const *data)
{
  (void)data ;
  return put_shebang(b)
   && buffer_puts(b,
     S6_EXTBINPREFIX "s6-envdir -- " S6_LINUX_INIT_TMPFS "/" CONTAINER_RESULTS "\n"
     EXECLINE_EXTBINPREFIX "multisubstitute\n{\n"
     "  importas -uD0 -- EXITCODE exitcode\n  "
     "  importas -uDh -- HALTCODE haltcode\n}\n"
     EXECLINE_EXTBINPREFIX "fdclose 1\n"
     EXECLINE_EXTBINPREFIX "fdclose 2\n"
     EXECLINE_EXTBINPREFIX "wait { }\n"
     EXECLINE_EXTBINPREFIX "ifelse -X { test $HALTCODE = r } { "
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-hpr -fnr }\n"
     EXECLINE_EXTBINPREFIX "ifelse -X { test $HALTCODE = p } { "
     S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-hpr -fnp }\n"
     EXECLINE_EXTBINPREFIX "exit $EXITCODE\n") >= 0 ;
}

static int s6_svscan_log_script (buffer *b, char const *data)
{
  if (!put_shebang(b)
   || buffer_puts(b, console || inns ?
       EXECLINE_EXTBINPREFIX "fdmove -c 1 2" :
       EXECLINE_EXTBINPREFIX "redirfd -w 1 /dev/null") < 0
   || buffer_puts(b, "\n"
       EXECLINE_EXTBINPREFIX "redirfd -rnb 0 " LOGGER_FIFO "\n") < 0) return 0 ;
  if (strcmp(log_user, "root"))
  {
    size_t sabase = satmp.len ;
    if (buffer_puts(b, S6_EXTBINPREFIX "s6-setuidgid ") < 0
     || !string_quote(&satmp, log_user, strlen(log_user))) return 0 ;
    if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0)
    {
      satmp.len = sabase ;
      return 0 ;
    }
    satmp.len = sabase ;
    if (buffer_puts(b, "\n") < 0) return 0 ;
  }
  if (buffer_puts(b, S6_EXTBINPREFIX "s6-log -bpd3 -- ") < 0) return 0 ;
  if (console && buffer_puts(b, "1 ") < 0) return 0 ;
  if (timestamp_style & 1 && buffer_puts(b, "t ") < 0
   || timestamp_style & 2 && buffer_puts(b, "T ") < 0
   || buffer_puts(b, S6_LINUX_INIT_TMPFS "/" UNCAUGHT_DIR "\n") < 0)
    return 0 ;
  (void)data ;
  return 1 ;
}

static int logouthookd_script (buffer *b, char const *data)
{
  (void)data ;
  return put_shebang(b)
   && buffer_puts(b,
    S6_EXTBINPREFIX "s6-ipcserver -1 -a 0700 -c 1000 -C 1000 -- " LOGOUTHOOKD_SOCKET "\n"
    S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-logouthookd\n") >= 0 ;
}

static int shutdownd_script (buffer *b, char const *data)
{
  size_t sabase = satmp.len ;
  char fmt[UINT_FMT] ;
  if (!put_shebang(b)
   || buffer_puts(b, S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-shutdownd -c ") < 0
   || !string_quote(&satmp, robase, strlen(robase))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
  satmp.len = sabase ;
  if (buffer_puts(b, " -g ") < 0
   || buffer_put(b, fmt, uint_fmt(fmt, finalsleep)) < 0
   || (inns && buffer_puts(b, " -C") < 0)
   || (nologger && buffer_puts(b, " -B") < 0)
   || buffer_puts(b, "\n") < 0) return 0 ;
  (void)data ;
  return 1 ;

 err:
  satmp.len = sabase ;
  return 0 ;    
}

static int runleveld_script (buffer *b, char const *data)
{
  size_t sabase = satmp.len ;
  if (!put_shebang(b)
   || buffer_puts(b,
    EXECLINE_EXTBINPREFIX "fdmove -c 2 1\n"
    EXECLINE_EXTBINPREFIX "fdmove 1 3\n"
    S6_EXTBINPREFIX "s6-ipcserver -1 -a 0700 -c 1 -- " RUNLEVELD_SOCKET "\n"
    S6_EXTBINPREFIX "s6-sudod -dt30000 --\n") < 0
   || !string_quote(&satmp, robase, strlen(robase))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
  satmp.len = sabase ;
  if (buffer_puts(b, "/scripts/runlevel\n") < 0) return 0 ;
  (void)data ;
  return 1 ;

 err:
  satmp.len = sabase ;
  return 0 ;
}

static int sig_script (buffer *b, char const *option)
{
  if (!put_shebang(b)
   || buffer_puts(b, S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init-shutdown ") < 0)
    return 0 ;
  if (!inns && buffer_puts(b, "-a ") < 0) return 0 ;
  if (buffer_puts(b, option) < 0
   || buffer_puts(b, " -- now\n") < 0)
    return 0 ;
  return 1 ;
}

static inline int stage1_script (buffer *b, char const *data)
{
  size_t sabase = satmp.len ;
  if (!put_shebang_options(b, "-S0")
   || buffer_puts(b, S6_LINUX_INIT_EXTBINPREFIX "s6-linux-init -c ") < 0
   || !string_quote(&satmp, robase, strlen(robase))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
  satmp.len = sabase ;
  {
    char fmt[UINT_OFMT] ;
    if (buffer_puts(b, " -m 00") < 0
     || buffer_put(b, fmt, uint_ofmt(fmt, initial_umask)) < 0) return 0 ;
  }
  if (initial_path)
  {
    if (buffer_puts(b, " -p ") < 0
     || !string_quote(&satmp, initial_path, strlen(initial_path))) return 0 ;
    if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
    satmp.len = sabase ;
  }
  if (env_store)
  {
    if (buffer_puts(b, " -s ") < 0
     || !string_quote(&satmp, env_store, strlen(env_store))) return 0 ;
    if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
    satmp.len = sabase ;
  }
  if (slashdev)
  {
    if (buffer_puts(b, " -d ") < 0
     || !string_quote(&satmp, slashdev, strlen(slashdev))) return 0 ;
    if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
    satmp.len = sabase ;
  }
  if (initdefault)
  {
    if (buffer_puts(b, " -D ") < 0
     || !string_quote(&satmp, initdefault, strlen(initdefault))) return 0 ;
    if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
    satmp.len = sabase ;
  }
  if (mounttype == 2)
  {
    if (buffer_puts(b, " -n") < 0) return 0 ;
  }
  else if (!mounttype)
  {
    if (buffer_puts(b, " -N") < 0) return 0 ;
  }
  if (inns && buffer_puts(b, " -C") < 0) return 0 ;
  if (nologger && buffer_puts(b, " -B") < 0) return 0 ;

  if (buffer_puts(b, " -- \"$@\"\n") < 0) return 0 ;
  (void)data ;
  return 1 ;

 err:
  satmp.len = sabase ;
  return 0 ;
}

static void cleanup (char const *base)
{
  int e = errno ;
  rm_rf(base) ;
  errno = e ;
}

static void auto_dir_internal (char const *base, char const *dir, uid_t uid, gid_t gid, unsigned int mode, int strict)
{
  size_t clen = strlen(base) ;
  size_t dlen = strlen(dir) ;
  char fn[clen + dlen + 2] ;
  memcpy(fn, base, clen) ;
  fn[clen] = dlen ? '/' : 0 ;
  memcpy(fn + clen + 1, dir, dlen + 1) ;

  if (mkdir(fn, mode) < 0)
  {
    if (errno != EEXIST || strict) goto err ;
  }
  else
  {
    if (chown(fn, uid, gid) < 0) goto err ;
    if (mode & 07000 && chmod(fn, mode) < 0) goto err ;
  }
  return ;

 err:
  cleanup(base) ;
  strerr_diefu2sys(111, "mkdir ", fn) ;
}

#define auto_dir(base, dir, uid, gid, mode) auto_dir_internal(base, dir, uid, gid, (mode), 1)

static void auto_file (char const *base, char const *file, char const *s, unsigned int n)
{
  size_t clen = strlen(base) ;
  size_t flen = strlen(file) ;
  char fn[clen + flen + 2] ;
  memcpy(fn, base, clen) ;
  fn[clen] = '/' ;
  memcpy(fn + clen + 1, file, flen + 1) ;
  if (!openwritenclose_unsafe(fn, s, n)
   || chmod(fn, 0644) == -1)
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
}

static void auto_symlink (char const *base, char const *name, char const *target)
{
  size_t clen = strlen(base) ;
  size_t dlen = strlen(name) ;
  char fn[clen + dlen + 2] ;
  memcpy(fn, base, clen) ;
  fn[clen] = '/' ;
  memcpy(fn + clen + 1, name, dlen + 1) ;
  if (symlink(target, fn) == -1)
  {
    cleanup(base) ;
    strerr_diefu4sys(111, "make a symlink named ", fn, " pointing to ", target) ;
  }
}

static void auto_fifo (char const *base, char const *fifo)
{
  size_t baselen = strlen(base) ;
  size_t fifolen = strlen(fifo) ;
  char fn[baselen + fifolen + 2] ;
  memcpy(fn, base, baselen) ;
  fn[baselen] = '/' ;
  memcpy(fn + baselen + 1, fifo, fifolen + 1) ;
  if (mkfifo(fn, 0600) < 0)
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "mkfifo ", fn) ;
  }
}

static void auto_script (char const *base, char const *file, writetobuf_func_t_ref scriptf, char const *data)
{
  char buf[4096] ;
  buffer b ;
  size_t baselen = strlen(base) ;
  size_t filelen = strlen(file) ;
  int fd ;
  char fn[baselen + filelen + 2] ;
  memcpy(fn, base, baselen) ;
  fn[baselen] = '/' ;
  memcpy(fn + baselen + 1, file, filelen + 1) ;
  fd = open_trunc(fn) ;
  if (fd < 0 || ndelay_off(fd) < 0 || fchmod(fd, 0755) < 0)
  {
    cleanup(base) ;
    strerr_diefu3sys(111, "open ", fn, " for script writing") ;
  }
  buffer_init(&b, &fd_writev, fd, buf, 4096) ;
  if (!(*scriptf)(&b, data) || !buffer_flush(&b))
  {
    cleanup(base) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
  fd_close(fd) ;
}

static void copy_script (char const *base, char const *name, int mandatory)
{
  size_t baselen = strlen(base) ;
  size_t namelen = strlen(name) ;
  size_t skellen = strlen(skeldir) ;
  char dst[baselen + sizeof("/scripts/") + namelen] ;
  char src[skellen + namelen + 2] ;
  memcpy(dst, base, baselen) ;
  memcpy(dst + baselen, "/scripts/", sizeof("/scripts/") - 1) ;
  memcpy(dst + baselen + sizeof("/scripts/") - 1, name, namelen + 1) ;
  memcpy(src, skeldir, skellen) ;
  src[skellen] = '/' ;
  memcpy(src + skellen + 1, name, namelen + 1) ;
  if (!filecopy_unsafe(src, dst, 0755) && mandatory)
  {
    cleanup(base) ;
    strerr_diefu4sys(111, "copy ", src, " to ", dst) ;
  }
}

static void auto_exec (char const *base, char const *name, char const *target)
{
  if (S6_LINUX_INIT_EXTBINPREFIX[0] == '/')
  {
    size_t len = strlen(target) ;
    char fn[sizeof(S6_LINUX_INIT_EXTBINPREFIX) + len] ;
    memcpy(fn, S6_LINUX_INIT_EXTBINPREFIX, sizeof(S6_LINUX_INIT_EXTBINPREFIX) - 1) ;
    memcpy(fn + sizeof(S6_LINUX_INIT_EXTBINPREFIX) - 1, target, len + 1) ;
    auto_symlink(base, name, fn) ;
  }
  else
    auto_script(base, name, &linewithargs_script, target) ;
}

static void make_env (char const *base, char const *envname, char *modif, size_t modiflen)
{
  size_t envnamelen = strlen(envname) ;
  auto_dir(base, envname, 0, 0, 0755) ;
  while (modiflen)
  {
    size_t len = strlen(modif) ;
    size_t pos = byte_chr(modif, len, '=') ;
    char fn[envnamelen + pos + 2] ;
    memcpy(fn, envname, envnamelen) ;
    fn[envnamelen] = '/' ;
    memcpy(fn + envnamelen + 1, modif, pos) ;
    fn[envnamelen + 1 + pos] = 0 ;
    
    if (pos + 1 < len)
    {
      modif[len] = '\n' ;
      auto_file(base, fn, modif + pos + 1, len - pos) ;
      modif[len] = 0 ;
    }
    else if (pos + 1 == len) auto_file(base, fn, "\n", 1) ;
    else auto_file(base, fn, "", 0) ;
    modif += len+1 ; modiflen -= len+1 ;
  }
}

static void getug (char const *base, char const *s, uid_t *uid, gid_t *gid)
{
  struct passwd *pw ;
  errno = 0 ;
  pw = getpwnam(s) ;
  if (!pw)
  {
    cleanup(base) ;
    if (!errno) strerr_diefu3x(100, "find user ", s, " in passwd database") ;
    else strerr_diefu2sys(111, "getpwnam for ", s) ;
  }
  *uid = pw->pw_uid ;
  *gid = pw->pw_gid ;
}

#ifdef S6_LINUX_INIT_UTMPD_PATH

static inline void auto_basedir (char const *base, char const *dir, uid_t uid, gid_t gid, unsigned int mode)
{
  size_t n = strlen(dir) ;
  char tmp[n + 1] ;
  for (size_t i = 0 ; i < n ; i++)
  {
    if ((dir[i] == '/') && i)
    {
      tmp[i] = 0 ;
      auto_dir_internal(base, tmp, uid, gid, mode, 0) ;
    }
    tmp[i] = dir[i] ;
  }
}

static int utmpd_script (buffer *b, char const *uw)
{
  size_t sabase = satmp.len ;
  if (!put_shebang(b)
   || buffer_puts(b,
    EXECLINE_EXTBINPREFIX "fdmove -c 2 1\n"
    S6_EXTBINPREFIX "s6-setuidgid ") < 0
   || !string_quote(&satmp, utmp_user, strlen(utmp_user))) return 0 ;
  if (buffer_put(b, satmp.s + sabase, satmp.len - sabase) < 0) goto err ;
  satmp.len = sabase ;
  if (buffer_puts(b, "\n"
    EXECLINE_EXTBINPREFIX "cd " S6_LINUX_INIT_TMPFS "/" UTMPS_DIR "\n"
    EXECLINE_EXTBINPREFIX "fdmove 1 3\n"
    S6_EXTBINPREFIX "s6-ipcserver -1 -c 1000 -- ") < 0) return 0 ;
  if (buffer_puts(b, uw[0] == 'u' ? UTMPS_UTMPD_PATH : UTMPS_WTMPD_PATH) < 0
   || buffer_puts(b, "\n"
    UTMPS_EXTBINPREFIX "utmps-") < 0
   || buffer_puts(b, uw) < 0
   || buffer_puts(b, "tmpd\n") < 0) return 0 ;
  return 1 ;

 err:
  satmp.len = sabase ;
  return 0 ;
}

static inline void make_utmps (char const *base)
{
  auto_dir(base, "run-image/" SCANDIR "/utmpd", 0, 0, 0755) ;
  auto_file(base, "run-image/" SCANDIR "/utmpd/notification-fd", "3\n", 2) ;
  auto_script(base, "run-image/" SCANDIR "/utmpd/run", &utmpd_script, "u") ;
  auto_dir(base, "run-image/" SCANDIR "/wtmpd", 0, 0, 0755) ;
  auto_file(base, "run-image/" SCANDIR "/wtmpd/notification-fd", "3\n", 2) ;
  auto_script(base, "run-image/" SCANDIR "/wtmpd/run", &utmpd_script, "w") ;
  {
    uid_t uid ;
    gid_t gid ;
    getug(base, utmp_user, &uid, &gid) ;
    auto_dir(base, "run-image/" UTMPS_DIR, uid, gid, 0755) ;
    auto_basedir(base, "run-image/" S6_LINUX_INIT_UTMPD_PATH, uid, gid, 0755) ;
    auto_basedir(base, "run-image/" S6_LINUX_INIT_WTMPD_PATH, uid, gid, 0755) ;
  }
}

#endif

static inline void make_image (char const *base)
{
  auto_dir(base, "run-image", 0, 0, 0755) ;
  auto_dir(base, "run-image/" SCANDIR, 0, 0, 0755) ;
  auto_dir(base, "run-image/" SCANDIR "/.s6-svscan", 0, 0, 0755) ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGTERM", &put_shebang_options, 0) ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGQUIT", &put_shebang_options, 0) ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGINT", &sig_script, "-r") ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGUSR1", &sig_script, "-p") ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGUSR2", &sig_script, "-h") ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGPWR", &sig_script, "-p") ;
  auto_script(base, "run-image/" SCANDIR "/.s6-svscan/SIGWINCH", &put_shebang_options, 0) ;

  if (!nologger)
  {
    uid_t uid ;
    gid_t gid ;
    getug(base, log_user, &uid, &gid) ;
    auto_dir(base, "run-image/" UNCAUGHT_DIR, uid, gid, 02750) ;
    auto_dir(base, "run-image/" SCANDIR "/" LOGGER_SERVICEDIR, 0, 0, 0755) ;
    auto_fifo(base, "run-image/" SCANDIR "/" LOGGER_SERVICEDIR "/" LOGGER_FIFO) ;
    auto_file(base, "run-image/" SCANDIR "/" LOGGER_SERVICEDIR "/notification-fd", "3\n", 2) ;
    auto_script(base, "run-image/" SCANDIR "/" LOGGER_SERVICEDIR "/run", &s6_svscan_log_script, 0) ;
  }

  auto_dir(base, "run-image/" SCANDIR "/" SHUTDOWND_SERVICEDIR, 0, 0, 0755) ;
  auto_fifo(base, "run-image/" SCANDIR "/" SHUTDOWND_SERVICEDIR "/" SHUTDOWND_FIFO) ;
  auto_script(base, "run-image/" SCANDIR "/" SHUTDOWND_SERVICEDIR "/run", &shutdownd_script, 0) ;

  if (inns)
  {
    auto_script(base, "run-image/" SCANDIR "/.s6-svscan/crash", &container_crash_script, 0) ;
    auto_script(base, "run-image/" SCANDIR "/.s6-svscan/finish", &container_exit_script, 0) ;
    auto_dir(base, "run-image/" CONTAINER_RESULTS, 0, 0, 0755) ;
    auto_file(base, "run-image/" CONTAINER_RESULTS "/exitcode", "0\n", 2) ;
  }
  else
  {
    auto_script(base, "run-image/" SCANDIR "/.s6-svscan/crash", &death_script, "crashed") ;
    auto_script(base, "run-image/" SCANDIR "/.s6-svscan/finish", &death_script, "exited") ;
    auto_dir(base, "run-image/" SCANDIR "/" RUNLEVELD_SERVICEDIR, 0, 0, 0755) ;
    auto_file(base, "run-image/" SCANDIR "/" RUNLEVELD_SERVICEDIR "/notification-fd", "3\n", 2) ;
    auto_script(base, "run-image/" SCANDIR "/" RUNLEVELD_SERVICEDIR "/run", &runleveld_script, 0) ;
  }

  if (logouthookd)
  {
    auto_dir(base, "run-image/" SCANDIR "/" LOGOUTHOOKD_SERVICEDIR, 0, 0, 0755) ;
    auto_file(base, "run-image/" SCANDIR "/" LOGOUTHOOKD_SERVICEDIR "/notification-fd", "1\n", 2) ;
    auto_script(base, "run-image/" SCANDIR "/" LOGOUTHOOKD_SERVICEDIR "/run", &logouthookd_script, 0) ;
  }

  if (early_getty)
  {
    auto_dir(base, "run-image/" SCANDIR "/" EARLYGETTY_SERVICEDIR, 0, 0, 0755) ;
    auto_script(base, "run-image/" SCANDIR "/" EARLYGETTY_SERVICEDIR "/run", &line_script, early_getty) ;
  }

#ifdef S6_LINUX_INIT_UTMPD_PATH
  if (utmp_user[0]) make_utmps(base) ;
#endif
}

static inline void make_scripts (char const *base)
{
  auto_dir(base, "scripts", 0, 0, 0755) ;
  if (!inns) copy_script(base, "runlevel", 1) ;
  copy_script(base, STAGE2, 1) ;
  copy_script(base, STAGE3, 1) ;
  copy_script(base, STAGE4, 0) ;
}

static inline void make_bins (char const *base)
{
  auto_dir(base, "bin", 0, 0, 0755) ;
  auto_script(base, "bin/init", &stage1_script, 0) ;
  auto_script(base, "bin/halt", &hpr_script, "h") ;
  auto_script(base, "bin/poweroff", &hpr_script, "p") ;
  auto_script(base, "bin/reboot", &hpr_script, "r") ;
  auto_exec(base, "bin/shutdown", "s6-linux-init-shutdown") ;
  if (!inns) auto_exec(base, "bin/telinit", "s6-linux-init-telinit") ;
}

int main (int argc, char const *const *argv, char const *const *envp)
{
  PROG = "s6-linux-init-maker" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, OPTION_STRING, &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : robase = l.arg ; break ;
        case 'u' : log_user = l.arg ; break ;
        case 'G' : early_getty = l.arg ; break ;
        case '1' : console = 1 ; break ;
        case 'L' : logouthookd = 1 ; break ;
        case 'p' : initial_path = l.arg ; break ;
        case 'm' : if (!uint0_oscan(l.arg, &initial_umask)) dieusage() ; break ;
        case 't' : if (!uint0_scan(l.arg, &timestamp_style)) dieusage() ; break ;
        case 'd' : slashdev = l.arg ; break ;
        case 's' : env_store = l.arg ; break ;
        case 'e' : if (!stralloc_catb(&satmp, l.arg, strlen(l.arg) + 1)) dienomem() ; break ;
        case 'q' : if (!uint0_scan(l.arg, &finalsleep)) dieusage() ; break ;
        case 'D' : initdefault = l.arg ; break ;
        case 'n' : mounttype = 2 ; break ;
        case 'N' : mounttype = 0 ; break ;
        case 'f' : skeldir = l.arg ; break ;
#ifdef S6_LINUX_INIT_UTMPD_PATH
        case 'U' : utmp_user = l.arg ; break ;
#endif
        case 'C' : inns = 1 ; break ;
        case 'B' : nologger = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (!argc) dieusage() ;

  if (robase[0] != '/')
    strerr_dief3x(100, "base directory ", robase, " is not absolute") ;
  if (slashdev && slashdev[0] != '/')
    strerr_dief3x(100, "devtmpfs directory ", slashdev, " is not absolute") ;
  if (env_store)
  {
    if (env_store[0] != '/')
      strerr_dief3x(100, "kernel environment store ", env_store, " is not absolute") ;
    if (!str_start(env_store, S6_LINUX_INIT_TMPFS "/"))
      strerr_warnw3x("kernel environment store ", env_store, " is not located under initial tmpfs " S6_LINUX_INIT_TMPFS) ;
  }
  if (timestamp_style > 3)
    strerr_dief1x(100, "-t timestamp_style must be 0, 1, 2 or 3") ;
  if (inns && slashdev)
    strerr_warnw1x("both -C and -d options given; are you sure your container does not come with a pre-mounted /dev?") ;

  umask(0) ;
  if (mkdir(argv[0], 0755) < 0)
    strerr_diefu2sys(111, "mkdir ", argv[0]) ;

  make_env(argv[0], ENVSTAGE1, satmp.s, satmp.len) ;
  satmp.len = 0 ;
  make_image(argv[0]) ;
  make_scripts(argv[0]) ;
  make_bins(argv[0]) ;
  return 0 ;
}
