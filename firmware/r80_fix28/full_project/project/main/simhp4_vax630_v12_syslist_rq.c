/*
 * simhP4 V12 probe-only MicroVAX II system list.
 *
 * V11C proved that a CPU-only list removes unrelated peripheral roots.
 * V12 adds only the primary RQ/MSCP controller for a READ-ONLY media probe.
 *
 * This is not yet the final 4.3BSD machine list.
 */

#include "vax_defs.h"

char sim_name[] = "MicroVAX II (KA630)";

void vax_init(void)
{
    sim_savename = "MicroVAX II (KA630)";
}

extern DEVICE cpu_dev;
extern DEVICE rq_dev;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &rq_dev,
    NULL
};

t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    (void)fileref;
    (void)cptr;
    (void)fnam;
    (void)flag;
    return SCPE_NOFNC;
}
