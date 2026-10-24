#include <stdio.h>
#include <stdlib.h>

#include "ward/support/fatal.h"

_Noreturn void ward_fatal(char const *file, int line, char const *fmt, ...) {
    fprintf(stderr, "ward: internal error at %s:%d: ", file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
    fflush(stderr);
    abort();
}
