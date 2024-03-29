#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "log-backtrace.h"

#define BT_BUF_SIZE 1024 * 10

void log_backtrace(const char *tag, const char *func_name)
{
    int nptrs;
    void *buffer[BT_BUF_SIZE];
    char **strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
       would produce similar output to the following: */

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL)
    {
        log_error("backtrace_symbols returned NULL");
        exit(EXIT_FAILURE);
    }

    log_debug("[%s] Function '%s' backtrace:", tag, func_name);
    for (size_t j = 0; j < nptrs - 1; j++)
    {
        log_debug("[%s]\t%s\n", tag, strings[j]);
    }

    free(strings);
}
