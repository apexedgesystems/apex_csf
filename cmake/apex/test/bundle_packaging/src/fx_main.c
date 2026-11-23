#include <stdio.h>

/* One source for the fixture's three executables: each prints the name it was
 * built with, and the C++-tool stand-in also names its shared library. */

#ifdef FX_USE_DEP
const char* fx_dep_name(void);
#endif

int main(void) {
#ifdef FX_USE_DEP
  printf("%s with %s\n", FX_NAME, fx_dep_name());
#else
  puts(FX_NAME);
#endif
  return 0;
}
