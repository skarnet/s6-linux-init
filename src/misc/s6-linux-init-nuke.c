/* ISC license. */

#include <signal.h>

int main (void)
{
  return kill(-1, SIGKILL) < 0 ;
}
