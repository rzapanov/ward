/// Error reporting used by the framework itself (not by test assertions).
#ifndef WARD_SUPPORT_FATAL_H
#define WARD_SUPPORT_FATAL_H

#include <stdarg.h>
#include <stddef.h>

/// Prints `file:line: message` to stderr and aborts.
/// Used only for framework invariant violations (a broken runner must not
/// silently report green tests).
_Noreturn void ward_fatal(char const *file, int line, char const *fmt, ...);

#define WARD_FATAL(...) ward_fatal(__FILE__, __LINE__, __VA_ARGS__)

#endif // WARD_SUPPORT_FATAL_H
