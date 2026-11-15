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


/* SIMHP4 V13 added device declarations */
extern DEVICE tlb_dev;
extern DEVICE rom_dev;
extern DEVICE nvr_dev;
extern DEVICE wtc_dev;
extern DEVICE sysd_dev;
extern DEVICE qba_dev;
extern DEVICE xq_dev; /* R79 enabled only after V12 PASS */
extern DEVICE clk_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;

/* SIMHP4_V13_STAGED_DEVICE_CLOSURE
 * Preserve the accepted V12 runtime contract during media validation:
 *   sim_devices[0] = CPU
 *   sim_devices[1] = RQ
 *   sim_devices[2] = NULL
 *
 * Only after V12 RESULT=PASS does v13_main.cpp call
 * simhp4_v13_activate_boot_devices(), replacing the visible table with the
 * minimal KA630 boot closure.  This keeps the V12 guard unchanged.
 */
DEVICE *sim_devices[13] = {
    &cpu_dev,
    &rq_dev,
    NULL
};

void simhp4_v13_activate_boot_devices(void)
{
    sim_devices[0]  = &cpu_dev;
    sim_devices[1]  = &tlb_dev;
    sim_devices[2]  = &rom_dev;
    sim_devices[3]  = &nvr_dev;
    sim_devices[4]  = &wtc_dev;
    sim_devices[5]  = &sysd_dev;
    sim_devices[6]  = &qba_dev;
    sim_devices[7]  = &clk_dev;
    sim_devices[8]  = &tti_dev;
    sim_devices[9]  = &tto_dev;
    sim_devices[10] = &rq_dev;
    sim_devices[11] = &xq_dev; /* R79 post-V12 Q-bus DEQNA */
    sim_devices[12] = NULL;

    /* R79 FIX6: V12 first cpu_reset() auto-configured CPU/RQ only.
     * The new XQ arrives after V12 PASS, so configure the COMPLETE V13
     * machine list before the next reset-time build_dib_tab().
     * The R75 sim_instr() hook is too late for that Q-bus registration.
     */
    xq_dev.flags &= ~DEV_DIS;
    t_stat r79_autorc = auto_config(NULL, 0);
    DIB *r79_xqdib = (DIB *)xq_dev.ctxt;
    if (r79_autorc != SCPE_OK || !r79_xqdib || r79_xqdib->ba == IOBA_AUTO) {
        sim_printf("SIMHP4_R79_XQ_AUTOCONFIG FAIL rc=%d csr=%06o\n",
                   (int)r79_autorc, r79_xqdib ? (unsigned)r79_xqdib->ba : 0u);
    } else {
        sim_printf("SIMHP4_R79_XQ_AUTOCONFIG PASS csr=%06o vec=%06o\n",
                   (unsigned)r79_xqdib->ba, (unsigned)r79_xqdib->vec);
    }

}

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
t_stat r;
int32 i;
uint32 origin, limit, step = 1;

if (flag)
    return sim_messagef (SCPE_NOFNC, "Command Not Implemented\n");
if (sim_switches & SWMASK ('R')) {
    origin = ROMBASE;
    limit = ROMBASE + ROMSIZE;
    }
else if (sim_switches & SWMASK ('N')) {
    origin = NVRBASE;
    limit = NVRBASE + NVRASIZE;
    step = 2;
    }
else {
    origin = 0;
    limit = (uint32) cpu_unit.capac;
    if (sim_switches & SWMASK ('O')) {
        origin = (int32) get_uint (cptr, 16, 0xFFFFFFFF, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        }
    }
while ((i = Fgetc (fileref)) != EOF) {
    if (origin >= limit)
        return SCPE_NXM;
    if (sim_switches & SWMASK ('R'))
        rom_wr_B (origin, i);
    else
        WriteB (origin, i);
    origin = origin + step;
    }
return SCPE_OK;
}
