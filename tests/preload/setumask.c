#include "compiler_utils.h"
#include <sys/stat.h>

/* set umask value to some known value independent of the environment */
pf_constructor void
setumask_init(void)
{
	umask(022);
}
