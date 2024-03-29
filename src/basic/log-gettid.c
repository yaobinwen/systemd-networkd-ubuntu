#include <sys/syscall.h>
#include <unistd.h>

#include "log.h"
#include "log-gettid.h"

void log_gettid(const char *tag, const char *func_name)
{
    pid_t tid = syscall(SYS_gettid);

    log_debug("[%s][%s] Current kernel thread ID: %d", tag, func_name, tid);
}
