#if !defined(ESP_PLATFORM)
#error "SIMHP4 V12E POSIX shim is ESP_PLATFORM-only"
#endif

#include <stdio.h>
#include <signal.h>
#include <errno.h>

/*
 * ESP32 has no process-spawn facility. Open SIMH scp.c uses popen/pclose
 * only for host-version reporting, not MicroVAX guest semantics.
 */
FILE *popen(const char *command, const char *type)
{
    (void)command;
    (void)type;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}

/*
 * V12 media probe does not execute the guest. POSIX process-signal delivery
 * has no ESP-IDF equivalent here; accept handler registration as a no-op.
 */
void (*signal(int sig, void (*func)(int)))(int)
{
    (void)sig;
    (void)func;
    return SIG_DFL;
}
