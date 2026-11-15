/*
 * simhP4 V11C probe-only MicroVAX II system list.
 *
 * Purpose:
 *   Keep the real Open SIMH KA630 CPU reset path while removing unrelated
 *   peripheral DEVICE references from sim_devices[] for the reset/model
 *   identity probe.  The pinned vendor vax630_syslist.c remains untouched.
 *
 * This is intentionally NOT the final 4.3BSD device configuration.
 */

#include "vax_defs.h"

char sim_name[] = "MicroVAX II (KA630)";

void vax_init(void)
{
    sim_savename = "MicroVAX II (KA630)";
}

extern DEVICE cpu_dev;

DEVICE *sim_devices[] = {
    &cpu_dev,
    NULL
};

/*
 * SCP expects an architecture sim_load() entry point.  Media loading is
 * deliberately disabled in V11C because this phase is reset/model only.
 */
t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    (void)fileref;
    (void)cptr;
    (void)fnam;
    (void)flag;
    return SCPE_NOFNC;
}
