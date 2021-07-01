#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

void
print_message_and_die(char* format, ...)
{
    va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
    exit(1);
}
