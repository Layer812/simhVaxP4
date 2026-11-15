/* vax_cpu.c: VAX CPU

   Copyright (c) 1998-2019, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          VAX central processor

   23-Apr-19    RMS     Added hook for unpredictable indexed immediate .aw
   14-Apr-19    RMS     Added hook for non-standard MxPR CC's
   31-Mar-17    RMS     Fixed uninitialized variable on FPD path (COVERITY)
   13-Mar-17    RMS     Fixed dangling else in show_opnd (COVERITY)
   20-Sep-11    MP      Fixed idle conditions for various versions of Ultrix, 
                        Quasijarus-4.3BSD, NetBSD and OpenBSD.
                        Note: Since NetBSD and OpenBSD are still actively 
                        developed operating systems, new versions of 
                        these OSes are moving targets with regard to 
                        providing idle detection.  At this time, recent versions 
                        of OpenBSD have veered from the traditional OS idle 
                        approach taken in the other BSD derived OSes.  
                        Determining a reasonable idle detection pattern does 
                        not seem possible for these versions.
   13-Sep-11    RMS     Fixed XFC, BPT to clear PSL<tp> before exception
                        (Camiel Vanderhoeven)
   23-Mar-11    RMS     Revised for new idle design (Mark Pizzolato)
   05-Jan-11    MP      Added Asynch I/O support
   24-Apr-10    RMS     Added OLDVMS idle timer option
                        Fixed bug in SET CPU IDLE
   21-May-08    RMS     Removed inline support
   28-May-08    RMS     Inlined instruction prefetch, physical memory routines
   13-Aug-07    RMS     Fixed bug in read access g-format indexed specifiers
   28-Apr-07    RMS     Removed clock initialization
   29-Oct-06    RMS     Added idle support
   22-May-06    RMS     Fixed format error in CPU history (Peter Schorn)
   10-May-06    RMS     Added -kesu switches for virtual addressing modes
                        Fixed bugs in examine virtual
                        Rewrote history function for greater usability
                        Fixed bug in reported VA on faulting cross-page write
   02-May-06    RMS     Fixed fault cleanup to clear PSL<tp>
                        Fixed ADAWI r-mode to preserve dst<31:16>
                        Fixed ACBD/G to test correct operand
                        Fixed access checking on modify-class specifiers
                        Fixed branch displacements in history buffer
                        (Tim Stark)
   17-Nov-05    RMS     Fixed CVTfi with integer overflow to trap if PSW<iv> set
   13-Nov-05    RMS     Fixed breakpoint test with 64b addresses
   25-Oct-05    RMS     Removed cpu_extmem
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   13-Jan-05    RMS     Fixed initial state of cpu_extmem
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   30-Sep-04    RMS     Added octaword specifier decodes and instructions
                        Moved model-specific routines to system module
   02-Sep-04    RMS     Fixed bug in EMODD/G, second word of quad dst not probed
   28-Jun-04    RMS     Fixed bug in DIVBx, DIVWx (Peter Trimmel)
   18-Apr-04    RMS     Added octaword macros
   25-Jan-04    RMS     Removed local debug logging support
                RMS,MP  Added extended physical memory support
   31-Dec-03    RMS     Fixed bug in set_cpu_hist
   21-Dec-03    RMS     Added autoconfiguration controls
   29-Oct-03    RMS     Fixed WriteB declaration (Mark Pizzolato)
   23-Sep-03    RMS     Revised instruction history for dynamic sizing
   17-May-03    RMS     Fixed operand order in EMODx
   23-Apr-03    RMS     Revised for 32b/64b t_addr
   05-Jan-02    RMS     Added memory size restore support
   25-Dec-02    RMS     Added instruction history (Mark Pizzolato)
   29-Sep-02    RMS     Revised to build dib_tab dynamically
   14-Jul-02    RMS     Added halt to console, infinite loop detection (Mark Pizzolato)
   02-May-02    RMS     Fixed bug in indexed autoincrement register logging
   30-Apr-02    RMS     Added TODR powerup routine
   18-Apr-02    RMS     Cleanup ambiguous signed left shifts
   15-Apr-02    RMS     Fixed bug in CASEL condition codes

   The register state for the VAX is:

        R[0:15]         general registers
        PSL<31:0>       processor status longword
         TP<30>         trace pending
         FPD<27>        first part done
         IS<26>         interrupt stack
         CM<25:24>      current mode
         PM<23:22>      previous mode
         IPL<20:16>     interrupt priority level
         PSW<15:0>      non-privileged processor status word
          DV<7>         decimal overflow trap enable
          FU<6>         floating underflow fault enable
          IV<5>         integer overflow trap enable
          T<4>          trace trap enable
          CC<3:0>       condition codes
        SCBB            system control block base
        PCBB            process control block base
        SBR             system page table base
        SLR             system page table length
        P0BR            process region 0 page table base
        P0LR            process region 0 page table length
        P1BR            process region 1 page table base
        P1LR            process region 1 page table length
        SIRR/SISR       software interrupt request/summary register
        ASTLVL          AST level register

   The VAX has a variable length instruction format with up to six operands:

        opcode byte
        operand 1 specifier
         :
        operand n specifier

   Each operand specifier is a byte consisting of an addressing mode, a
   register, and possibly 1-8 bytes of extension:

        number  name        extension   mnemonic        operation

        0-3     short literal   -       #n      op <- specifier
        4       index           -       [Rn]    index by Rn
        5       register        -       Rn      op <- Rn
        6       register def    -       (Rn)    op <- M[Rn]
        7       autodecrement   -       -(Rn)   Rn <- Rn - length
                                                op <- M[Rn]
        8       autoincrement   -       (Rn)+   op <- M[Rn]
                                                Rn <- Rn + length
        9       auto deferred   -       @(Rn)+  op <- M[M[Rn]]
                                                Rn <- Rn + 4
        A       byte displ      byte d  d(Rn)   op <- M[Rn + sxt.d]
        B       byte displ def  byte d  @d(Rn)  op <- M[M[Rn + sxt.d]]
        C       word displ      word d  d(Rn)   op <- M[Rn + sxt.d]
        D       word displ def  word d  @d(Rn)  op <- M[M[Rn + sxt.d]]
        E       long displ      long d  d(Rn)   op <- M[Rn + d]
        F       long displ def  long d  @d(Rn)  op <- M[M[Rn + d]]

   When the general register is the PC, certain modes are forbidden, and
   others have special interpretations:

        4F      index           fault
        5F      register        fault
        6F      register def    fault
        7F      autodecrement   fault
        8F      immediate       1-8B    #imm    op <- imm
        9       absolute        4B      @#imm   op <- M[imm]
        A       byte relative   byte d  d(Rn)   op <- M[PC + sxt.d]
        B       byte rel def    byte d  @d(Rn)  op <- M[M[PC + sxt.d]]
        C       word relative   word d  d(Rn)   op <- M[PC + sxt.d]
        D       word rel def    word d  @d(Rn)  op <- M[M[PC + sxt.d]]
        E       long relative   long d  d(Rn)   op <- M[PC + d]
        F       long rel def    long d  @d(Rn)  op <- M[M[PC + d]]

   This routine is the instruction decode routine for the VAX.  It
   is called from the simulator control program to execute instructions
   in simulated memory, starting at the simulated PC.  It runs until an
   enabled exception is encountered.

   General notes:

   1. Traps and interrupts.  Variable trpirq microencodes the outstanding
        trap request (if any) and the level of the highest outstanding
        interrupt (if any).

   2. Interrupt requests are maintained in the int_req array, one word per
      interrupt level, one bit per device.

   3. Adding I/O devices.  These modules must be modified:

        vax_defs.h      add device address and interrupt definitions
        vax_sys.c       add sim_devices table entry
*/

/* Definitions */

#include "vax_defs.h"
#include "esp_timer.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* SIMHP4_V13_R10R3_EVENT_BOUNDARY_HOST_YIELD
 *
 * ESP32 host scheduling only.  Do not modify SIMH guest-visible state,
 * sim_interval, PC, PSL, interrupt ordering, or device deadlines.
 *
 * The VAX CPU runs inside ESP-IDF's main task.  Continuous sim_instr()
 * execution can starve IDLE0 long enough for the Task Watchdog to report it.
 * At a normal SIMH event-processing boundary, give CPU0 back to FreeRTOS for
 * one RTOS tick, but at most once per ~10 ms of host time.
 */
/* SIMHP4_R80_REV10_POWER: passive flag from WiFi worker, no VAX state. */
extern int simhp4_r80_wifi_power_phase(void);
static void simhp4_v13r10_host_yield(void)
{
    const int wifi_phase = simhp4_r80_wifi_power_phase();
    static TickType_t last_yield_tick = 0;
    static int announced = 0;
    TickType_t period = pdMS_TO_TICKS(wifi_phase ? 5 : 10);
    const TickType_t now = xTaskGetTickCount();

    if (period == 0)
        period = 1;

    if ((TickType_t)(now - last_yield_tick) >= period) {
        last_yield_tick = now;
        if (!announced) {
            announced = 1;
            ESP_LOGI("SIMHP4_R10",
                     "HOST_YIELD R6R27A boundary=sim_process_event eligible_ms=10 sleep_spacing_ms=100 delay_ticks=1");
        }
        /* SIMHP4_R6R27A_HOST_YIELD_OBSERVER: minimum 100ms spacing of actual 1-tick host sleeps.
             * Existing R10 >=10ms event-boundary eligibility remains intact.
             * Never skips a SIMH event, VAX instruction, or guest time.
             * CPU0 IDLE task gets a 1-tick sleep opportunity at most once per 100ms
             * provided SIMH event processing continues.
             */
            extern volatile uint32 simhp4_r6r27a_host_yield_n;
            extern volatile uint32 simhp4_r6r27a_host_yield_us;
            extern volatile uint32 simhp4_r6r27a_host_yield_max_us;
            static uint32 r6r27a_last_sleep_us = 0u;
            const uint32 r6r27a_now_us = (uint32)esp_timer_get_time();
            if ((uint32)(r6r27a_now_us - r6r27a_last_sleep_us) >= (wifi_phase ? 6000u : 100000u)) {
                r6r27a_last_sleep_us = r6r27a_now_us;
                const uint32 hy_start = (uint32)esp_timer_get_time();
                vTaskDelay(wifi_phase ? 2 : 1);
                const uint32 hy_us = (uint32)esp_timer_get_time() - hy_start;
                ++simhp4_r6r27a_host_yield_n;
                simhp4_r6r27a_host_yield_us += hy_us;
                if (hy_us > simhp4_r6r27a_host_yield_max_us)
                    simhp4_r6r27a_host_yield_max_us = hy_us;
            }
    }
}
#endif



#define UNIT_V_CONH     (UNIT_V_UF + 0)                 /* halt to console */
#define UNIT_V_MSIZE    (UNIT_V_UF + 1)                 /* dummy */
#define UNIT_CONH       (1u << UNIT_V_CONH)
#define UNIT_MSIZE      (1u << UNIT_V_MSIZE)
#define GET_CUR         acc = ACC_MASK (PSL_GETCUR (PSL))

#define OPND_SIZE       16
#define INST_SIZE       52
#define op0             opnd[0]
#define op1             opnd[1]
#define op2             opnd[2]
#define op3             opnd[3]
#define op4             opnd[4]
#define op5             opnd[5]
#define op6             opnd[6]
#define op7             opnd[7]
#define op8             opnd[8]
#define CHECK_FOR_PC    if (rn == nPC) \
                            RSVD_ADDR_FAULT
#define CHECK_FOR_SP    if (rn >= nSP) \
                            RSVD_ADDR_FAULT
#define CHECK_FOR_AP    if (rn >= nAP) \
                            RSVD_ADDR_FAULT
#define WRITE_B(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_BYTE, WA); \
                        else R[rn] = (R[rn] & ~BMASK) | ((r) & BMASK)
#define WRITE_W(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_WORD, WA); \
                        else R[rn] = (R[rn] & ~WMASK) | ((r) & WMASK)
#define WRITE_L(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_LONG, WA); \
                        else R[rn] = (r)
#define WRITE_Q(arl,arh)  if (spec > (GRN | nPC)) { \
                        if ((Test (va + 7, WA, &mstat) >= 0) || \
                            (Test (va, WA, &mstat) < 0)) \
                            Write (va, arl, L_LONG, WA); \
                            Write (va + 4, arh, L_LONG, WA); \
                            } \
                        else { \
                            if (rn >= nSP) \
                                RSVD_ADDR_FAULT; \
                            R[rn] = arl; \
                            R[rn + 1] = arh; \
                            } \
                        r = arl; \
                        rh = arh


uint32 *M = NULL;                                       /* memory */
int32 R[16];                                            /* registers */
int32 STK[5];                                           /* stack pointers */
int32 PSL;                                              /* PSL */
int32 SCBB = 0;                                         /* SCB base */
int32 PCBB = 0;                                         /* PCB base */
int32 P0BR = 0;                                         /* P0 mem mgt */
int32 P0LR = 0;
int32 P1BR = 0;                                         /* P1 mem mgt */
int32 P1LR = 0;
int32 SBR = 0;                                          /* S0 mem mgt */
int32 SLR = 0;
int32 SISR;                                             /* swre int req */
int32 ASTLVL;                                           /* AST level */
int32 mapen;                                            /* map enable */
int32 pme;                                              /* perf mon enable */
int32 trpirq;                                           /* trap/intr req */
int32 in_ie = 0;                                        /* in exc, int */
int32 recq[6];                                          /* recovery queue */
int32 recqptr;                                          /* recq pointer */
int32 hlt_pin = 0;                                      /* HLT pin intr */
int32 mem_err = 0;
int32 crd_err = 0;
int32 p1 = 0, p2 = 0;                                   /* fault parameters */
int32 fault_PC;                                         /* fault PC */
int32 mxpr_cc_vc = 0;                                   /* MxPR V,C bits */
int32 pcq_p = 0;                                        /* PC queue ptr */
int32 badabo = 0;
int32 cpu_instruction_set = CPU_INSTRUCTION_SET;        /* Instruction Groups  */
int32 cpu_astop = 0;
int32 mchk_va, mchk_ref;                                /* mem ref param */
/* SIMHP4_R6R27A_EVENT_GATE -- event boundary only; NO per-instruction observer.
 * No NEW guest fast-forward, 25% pacing, or clock/IRQ/device modification.
 */
#include "esp_timer.h"
volatile uint32 simhp4_r6r27a_events = 0;
volatile uint32 simhp4_r6r27a_pre_nonpositive = 0;
volatile uint32 simhp4_r6r27a_after_le1 = 0;
volatile uint32 simhp4_r6r27a_after_2_99 = 0;
volatile uint32 simhp4_r6r27a_after_ge100 = 0;
volatile uint32 simhp4_r6r27a_event_us = 0;
volatile uint32 simhp4_r6r27a_event_max_us = 0;
volatile int32 simhp4_r6r27a_last_pre = 0;
volatile int32 simhp4_r6r27a_last_post = 0;
volatile uint32 simhp4_r6r27a_last_pc = 0;
volatile uint32 simhp4_r6r27a_last_status = 0;
volatile uint32 simhp4_r6r27a_host_yield_n = 0;
volatile uint32 simhp4_r6r27a_host_yield_us = 0;
volatile uint32 simhp4_r6r27a_host_yield_max_us = 0;
volatile uint32 simhp4_r6r27a_irq_all = 0;
static t_stat simhp4_r6r27a_process_event(void)
{
    const int32 pre = sim_interval;
    const uint32 pc = (uint32)PC;
    const uint32 start = (uint32)esp_timer_get_time();
    t_stat result = sim_process_event ();
    const uint32 elapsed = (uint32)esp_timer_get_time() - start;
    const int32 post = sim_interval;
    ++simhp4_r6r27a_events;
    if (pre <= 0) ++simhp4_r6r27a_pre_nonpositive;
    if (post <= 1) ++simhp4_r6r27a_after_le1;
    else if (post < 100) ++simhp4_r6r27a_after_2_99;
    else ++simhp4_r6r27a_after_ge100;
    simhp4_r6r27a_event_us += elapsed;
    if (elapsed > simhp4_r6r27a_event_max_us)
        simhp4_r6r27a_event_max_us = elapsed;
    simhp4_r6r27a_last_pre = pre;
    simhp4_r6r27a_last_post = post;
    simhp4_r6r27a_last_pc = pc;
    simhp4_r6r27a_last_status = (uint32)result;
    return result;
}

int32 ibufl, ibufh;                                     /* prefetch buf */
int32 ibcnt, ppc;                                       /* prefetch ctl */
uint32 cpu_idle_mask =                                  /* idle mask */
#if defined (VAX_411) || defined (VAX_412)
                       VAX_IDLE_INFOSERVER;
uint32 cpu_idle_type = 2;                               /* default INFOSERVER */
#else
                       VAX_IDLE_VMS;
uint32 cpu_idle_type = 1;                               /* default VMS */
#endif
int32 extra_bytes;                                      /* bytes referenced by current string instruction */
jmp_buf save_env;
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
InstHistory *hst = NULL;                                /* instruction history */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
int32 hst_switches;                                     /* history option switches */
FILE *hst_log;                                          /* history log file */
int32 hst_log_p;                                        /* history last log written pointer */
int32 step_out_nest_level = 0;                          /* step to call return - nest level */

const uint32 byte_mask[33] = { 0x00000000,
 0x00000001, 0x00000003, 0x00000007, 0x0000000F,
 0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
 0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
 0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
 0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
 0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
 };
const uint32 byte_sign[33] = { 0x00000000,
 0x00000001, 0x00000002, 0x00000004, 0x00000008,
 0x00000010, 0x00000020, 0x00000040, 0x00000080,
 0x00000100, 0x00000200, 0x00000400, 0x00000800,
 0x00001000, 0x00002000, 0x00004000, 0x00008000,
 0x00010000, 0x00020000, 0x00040000, 0x00080000,
 0x00100000, 0x00200000, 0x00400000, 0x00800000,
 0x01000000, 0x02000000, 0x04000000, 0x08000000,
 0x10000000, 0x20000000, 0x40000000, 0x80000000
 };
const uint32 align[4] = {
 0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF
 };

/* External and forward references */

extern int32 sys_model;

t_stat cpu_reset (DEVICE *dptr);
t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs);
t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_show_virt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_idle (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_instruction_set (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_instruction_set (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *cpu_description (DEVICE *dptr);
int32 cpu_get_vsw (int32 sw);
static SIM_INLINE int32 get_istr (int32 lnt, int32 acc);
int32 ReadOcta (int32 va, int32 *opnd, int32 j, int32 acc);
t_bool cpu_show_opnd (FILE *st, InstHistory *h, int32 line);
t_stat cpu_show_hist_records (FILE *st, t_bool do_header, int32 start, int32 count);
int32 cpu_emulate_exception (int32 *opnd, int32 cc, int32 opc, int32 acc);
void cpu_idle (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX|UNIT_BINK, INITMEMSIZE)
    };

const char *psl_modes[] = {"K", "E", "S", "U"};


BITFIELD cpu_psl_bits[] = {
    BIT(C),                                 /* Carry */
    BIT(V),                                 /* Overflow */
    BIT(Z),                                 /* Zero */
    BIT(N),                                 /* Negative */
    BIT(T),                                 /* trace */
    BIT(IV),                                /* Integer overflow */
    BIT(FU),                                /* Floating underflow */
    BIT(DV),                                /* Decimal overflow */
    BITNCF(8),                              /* MBZ */
    BITFFMT(IPL,5,%d),                      /* IPL */
    BITNCF(1),                              /* MBZ */
    BITFNAM(PRVMOD,2,psl_modes),            /* Previous Access Mode */
    BITFNAM(CURMOD,2,psl_modes),            /* Current Access Mode */
    BIT(IS),                                /* Interrupt Stack */
    BIT(FPD),                               /* First Part Done */
    BITNCF(2),                              /* MBZ */
    BIT(TP),                                /* Trace Pending */
    BIT(CM),                                /* Compatibility Mode */
    ENDBITS
};

REG cpu_reg[] = {
    { HRDATAD (PC,      R[nPC], 32, "program counter") },
    { HRDATAD (R0,        R[0], 32, "General Purpose Register 0") },
    { HRDATAD (R1,        R[1], 32, "General Purpose Register 1") },
    { HRDATAD (R2,        R[2], 32, "General Purpose Register 2") },
    { HRDATAD (R3,        R[3], 32, "General Purpose Register 3") },
    { HRDATAD (R4,        R[4], 32, "General Purpose Register 4") },
    { HRDATAD (R5,        R[5], 32, "General Purpose Register 5") },
    { HRDATAD (R6,        R[6], 32, "General Purpose Register 6") },
    { HRDATAD (R7,        R[7], 32, "General Purpose Register 7") },
    { HRDATAD (R8,        R[8], 32, "General Purpose Register 8") },
    { HRDATAD (R9,        R[9], 32, "General Purpose Register 9") },
    { HRDATAD (R10,      R[10], 32, "General Purpose Register 10") },
    { HRDATAD (R11,      R[11], 32, "General Purpose Register 11") },
    { HRDATAD (R12,      R[12], 32, "General Purpose Register 12") },
    { HRDATAD (R13,      R[13], 32, "General Purpose Register 13") },
    { HRDATAD (R14,      R[14], 32, "General Purpose Register 14") },
    { HRDATAD (AP,      R[nAP], 32, "Alias for R12") },
    { HRDATAD (FP,      R[nFP], 32, "Alias for R13") },
    { HRDATAD (SP,      R[nSP], 32, "Alias for R14") },
    { HRDATADF(PSL,        PSL, 32, "processor status longword", cpu_psl_bits) },
    { HRDATAD (CC,         PSL,  4, "condition codes, PSL<3:0>") },
    { HRDATAD (KSP,       KSP,  32, "kernel stack pointer") },
    { HRDATAD (ESP,       ESP,  32, "executive stack pointer") },
    { HRDATAD (SSP,       SSP,  32, "supervisor stack pointer") },
    { HRDATAD (USP,       USP,  32, "user stack pointer") },
    { HRDATAD (IS,         IS,  32, "interrupt stack pointer") },
    { HRDATAD (SCBB,      SCBB, 32, "system control block base") },
    { HRDATAD (PCBB,      PCBB, 32, "process control block base") },
    { HRDATAD (P0BR,      P0BR, 32, "P0 base register") },
    { HRDATAD (P0LR,      P0LR, 22, "P0 length register") },
    { HRDATAD (P1BR,      P1BR, 32, "P1 base register") },
    { HRDATAD (P1LR,      P1LR, 22, "P1 length register") },
    { HRDATAD (SBR,        SBR, 32, "system base register") },
    { HRDATAD (SLR,        SLR, 22, "system length register") },
    { HRDATAD (SISR,      SISR, 16, "software interrupt summary register") },
    { HRDATAD (ASTLVL,  ASTLVL,  4, "AST level register") },
    { FLDATAD (MAPEN,    mapen,  0, "memory management enable") },
    { FLDATAD (PME,        pme,  0, "performance monitor enable") },
    { HRDATAD (TRPIRQ,  trpirq,  8, "trap/interrupt pending") },
    { FLDATAD (CRDERR, crd_err,  0, "correctible read data error flag") },
    { FLDATAD (MEMERR, mem_err,  0, "memory error flag") },
    { FLDATA (HLTPIN, hlt_pin,  0) },
    { HRDATA (IDLE_MASK, cpu_idle_mask, 16), REG_HIDDEN },
    { DRDATA (IDLE_INDX, cpu_idle_type, 4), REG_HRO },
    { DRDATA (IDLE_ENAB, sim_idle_enab, 4), REG_HRO },
    { BRDATAD (PCQ, pcq, 16, 32, PCQ_SIZE, "PC prior to last PC change or interrupt;"), REG_RO+REG_CIRC },
    { HRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (BADABO, badabo, 32), REG_HRO },
    { HRDATAD (WRU, sim_int_char, 8, "interrupt character") },
    { HRDATA (MODEL, sys_model, 32), REG_HRO },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_CONH, 0, "HALT to SIMH", "SIMHALT", NULL, NULL, NULL, "Set HALT to trap to simulator" },
    { UNIT_CONH, UNIT_CONH, "HALT to console", "CONHALT", NULL, NULL, NULL, "Set HALT to trap to console ROM" },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE{=VMS|ULTRIX|ULTRIX-1.X|ULTRIXOLD|NETBSD|NETBSDOLD|OPENBSD|OPENBSDOLD|QUASIJARUS|32V|ELN|MDM|INFOSERVER}{:n}", &cpu_set_idle, &cpu_show_idle, NULL, "Display idle detection mode" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL, NULL,  "Disables idle detection" },
    MEM_MODIFIERS,   /* Model specific memory modifiers from vaxXXX_defs.h */
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP|MTAB_NC, 0, "HISTORY", "HISTORY=n",
      &cpu_set_hist, &cpu_show_hist, NULL, "Enable/Display instruction history" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &cpu_show_virt, NULL, "show translation for address arg in KESU mode" },
    CPU_MODEL_MODIFIERS  /* Model specific cpu modifiers from vaxXXX_defs.h */
    CPU_INST_MODIFIERS   /* Model specific cpu instruction modifiers from vaxXXX_defs.h */
    { 0 }
    };

DEBTAB cpu_deb[] = {
    { "INTEXC",    LOG_CPU_I,           "interrupt and exception activities" },
    { "REI",       LOG_CPU_R,           "REI activities" },
    { "ABORT",     LOG_CPU_A,           "Abort activities" },
    { "CONTEXT",   LOG_CPU_P,           "context switching activities" },
    { "RSVDFAULT", LOG_CPU_FAULT_RSVD,  "reserved fault activities" },
    { "FLTFAULT",  LOG_CPU_FAULT_FLT,   "floating fault activities" },
    { "CMODFAULT", LOG_CPU_FAULT_CMODE, "cmode fault activities" },
    { "MCHKFAULT", LOG_CPU_FAULT_MCHK,  "machine check fault activities" },
    { "EMULFAULT", LOG_CPU_FAULT_EMUL,  "instruction emulation fault activities" },
    { NULL, 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 32, 1, 16, 8,
    &cpu_ex, &cpu_dep, &cpu_reset,
    &cpu_boot, NULL, NULL,
    NULL, DEV_DYNM | DEV_DEBUG, 0,
    cpu_deb, &cpu_set_size, NULL, &cpu_help, NULL, NULL,
    &cpu_description
    };

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "model=");
return cpu_print_model (st);
}

const char *cpu_description (DEVICE *dptr)
{
static char buf[80];
uint32 min_mem = 4096, max_mem = 0;
MTAB *mptr;

for (mptr = dptr->modifiers; mptr && (mptr->mask != 0); mptr++) {
    if (mptr->valid != &cpu_set_size)
        continue;
    if ((mptr->match >> 20) < min_mem)
        min_mem = (mptr->match >> 20);
    if ((mptr->match >> 20) > max_mem)
        max_mem = (mptr->match >> 20);
    }
sprintf (buf, "VAX CPU with %dMB-%dMB of memory", (int)min_mem, (int)max_mem);
return buf;
}

/* R75 FIX2: actual compiled VAX CPU entrypoint. */
extern void r75_xq_preboot_prepare(void);
extern void r79_xq_report_post_dib(void);
t_stat sim_instr (void)
{
    r75_xq_preboot_prepare(); /* BEFORE first guest instruction */
volatile int32 opc = 0, cc;                             /* used by setjmp */
volatile int32 acc;                                     /* set by setjmp */
int abortval;
t_stat ret;
int32 r = 0, rh = 0, temp = 0;
int32 spec = 0, disp = 0, rn = 0, index = 0, numspec = 0;
int32 vfldrp1 = 0, brdisp = 0, flg = 0, mstat = 0;
uint32 va = 0, iad = 0;
int32 opnd[OPND_SIZE];                                  /* operand queue */

if ((ret = build_dib_tab ()) != SCPE_OK)                /* build, chk dib_tab */
    return ret;
r79_xq_report_post_dib(); /* after successful Q-bus registration */
if ((PSL & PSL_MBZ) ||                                  /* validate PSL<mbz> */
    ((PSL & PSL_CM) && BadCmPSL (PSL)) ||               /* validate PSL<cm> */
    ((PSL_GETCUR (PSL) != KERN) &&                      /* esu => is, ipl = 0 */
        (PSL & (PSL_IS|PSL_IPL))) ||
    ((PSL & PSL_IS) && ((PSL & PSL_IPL) == 0)))         /* is => ipl > 0 */
    return sim_messagef (SCPE_STOP, "Unreasonable PSL value: %08X\r\n", PSL);
cc = PSL & CC_MASK;                                     /* split PSL */
PSL = PSL & ~CC_MASK;
in_ie = 0;                                              /* not in exc */
set_map_reg ();                                         /* set map reg */
GET_CUR;                                                /* set access mask */
SET_IRQL;                                               /* eval interrupts */
FLUSH_ISTR;                                             /* clear prefetch */

abortval = setjmp (save_env);                           /* set abort hdlr */
if (abortval > 0) {                                     /* sim stop? */
    PSL = PSL | cc;                                     /* put PSL together */
    pcq_r->qptr = pcq_p;                                /* update pc q ptr */
    if (hst_log) {                                      /* auto logging history? */
        cpu_show_hist_records (hst_log, FALSE, hst_log_p, (hst_p < hst_log_p) ? hst_lnt - (hst_log_p - hst_p) : hst_p - hst_log_p);
        hst_log_p = hst_p;                              /* record everything logged */
        }
    return abortval;                                    /* return to SCP */
    }
else if (abortval < 0) {                                /* mm or rsrv or int */
    int32 i, delta;
    if ((PSL & PSL_FPD) == 0) {                         /* FPD? no recovery */
        for (i = 0; i < recqptr; i++) {                 /* unwind inst */
            int32 rrn, rlnt;
            rrn = RQ_GETRN (recq[i]);                   /* recover reg # */
            rlnt = DR_LNT (RQ_GETLNT (recq[i]));        /* recovery lnt */
            if (recq[i] & RQ_DIR)
                R[rrn] = R[rrn] - rlnt;
            else R[rrn] = R[rrn] + rlnt;
            }
        }
    sim_debug (LOG_CPU_A, &cpu_dev, "abort=%08X, fault_PC=%08x, SP=%08x, PC=%08x, PSL=%08x ",
                 -abortval, fault_PC, PC, SP, PSL);
    sim_debug_bits(LOG_CPU_A, &cpu_dev, cpu_psl_bits, PSL, PSL, 1);
    PSL = PSL & ~PSL_TP;                                /* clear <tp> */
    recqptr = 0;                                        /* clear queue */
    delta = PC - fault_PC;                              /* save delta PC */
    SETPC (fault_PC);                                   /* restore PC */
    switch (-abortval) {                                /* case on abort code */

    case SCB_RESIN:                                     /* rsrv inst fault */
    case SCB_RESAD:                                     /* rsrv addr fault */
    case SCB_RESOP:                                     /* rsrv opnd fault */
        if (in_ie)                                      /* in exc? panic */
            ABORT (STOP_INIE);
        cc = intexc (-abortval, cc, 0, IE_EXC);         /* take exception */
        GET_CUR;                                        /* PSL<cur> changed */
        break;

    case SCB_CMODE:                                     /* comp mode fault */
    case SCB_ARITH:                                     /* arithmetic fault */
        if (in_ie)                                      /* in exc? panic */
            ABORT (STOP_INIE);
        cc = intexc (-abortval, cc, 0, IE_EXC);         /* take exception */
        GET_CUR;
        in_ie = 1;
        Write (SP - 4, p1, L_LONG, WA);                 /* write arith param */
        SP = SP - 4;
        in_ie = 0;
        break;

    case SCB_ACV:                                       /* ACV fault */
    case SCB_TNV:                                       /* TNV fault */
        if (in_ie) {                                    /* in exception? */
            if (PSL & PSL_IS)                           /* on is? panic */
                ABORT (STOP_INIE);
            cc = intexc (SCB_KSNV, cc, 0, IE_SVE);      /* ksnv */
            GET_CUR;
            }
        else {
            cc = intexc (-abortval, cc, 0, IE_EXC);     /* take exception */
            GET_CUR;
            in_ie = 1;
            Write (SP - 8, p1, L_LONG, WA);             /* write mm params */
            Write (SP - 4, p2, L_LONG, WA);
            SP = SP - 8;
            in_ie = 0;
            }
        break;

    case SCB_MCHK:                                      /* machine check */
        sim_debug (LOG_CPU_FAULT_MCHK, &cpu_dev, "%s fault_PC=%08x, PSL=%08x, cc=%08x, PC=%08x, delta-%08X, p1=%08X\n",
                                                 opcode[opc], fault_PC, PSL, cc, PC, delta, p1);
        cc = machine_check (p1, opc, cc, delta);        /* system specific */
        in_ie = 0;
        GET_CUR;                                        /* PSL<cur> changed */
        break;

    case 1:                                             /* interrupt */
        break;                                          /* just proceed */
    default:                                            /* other */
        badabo = abortval;                              /* save code */
        ABORT (STOP_UNKABO);                            /* panic */
        }                                               /* end case */
    }                                                   /* end else */

/* Main instruction loop */

for ( ;; ) {
    int32 i, j;

/* Optionally record instruction history results from prior instruction */

    if (hst_lnt) {
        InstHistory *hlast = &hst[hst_p ? hst_p-1 : hst_lnt -1];

        switch (DR_GETRES(drom[hlast->opc][0]) << DR_V_RESMASK) {
            case RB_O:
            case RB_OB:
            case RB_OW:
            case RB_OL:
            case RB_OQ:
                break;
            case RB_Q:
                hlast->res[1] = rh;
                hlast->res[0] = r;
                break;
            case RB_B:
            case RB_W:
            case RB_L:
                hlast->res[0] = r;
                break;
            case RB_R5:
                hlast->res[5] = R[5];
                hlast->res[4] = R[4];
            case RB_R3:
                hlast->res[3] = R[3];
                hlast->res[2] = R[2];
            case RB_R1:
                hlast->res[1] = R[1];
            case RB_R0:
                hlast->res[0] = R[0];
                break;
            case RB_SP:
                hlast->res[0] = Read (SP, L_LONG, RA);
                break;
            default:
                break;
            }
        }

    if (cpu_astop) {
        cpu_astop = 0;
        ABORT (SCPE_STOP);
        }

#if defined(ESP_PLATFORM)
    /* SIMHP4_V14K_HOST_EQUIVALENT_RAM_CLEAR
     *
     * Exact KA630 ROM loop:
     *   20041F14  CLRQ -(R1)
     *   20041F16  TSTL R1
     *   20041F18  BGTR 20041F14
     *
     * Replace only the complete effect of this loop:
     *   zero guest RAM [0,R1)
     *   preserve guest RAM [R1,MEMSIZE)
     *   R1 = 0
     *   condition code = Z
     *   PC = 20041F1A
     */
    {
    static int simhp4_r14k_done = 0;
    if ((!simhp4_r14k_done) &&
        ((uint32)PC == (ROMBASE + 0x1F14u)) &&
        ((uint32)R[1] > 0u) &&
        ((uint32)R[1] <= (uint32)MEMSIZE) &&
        (((uint32)R[1] & 7u) == 0u)) {
        uint32 clear_bytes = (uint32)R[1];
        uint32 clear_words = clear_bytes >> 2;
        uint32 wi;

        ESP_LOGI("SIMHP4_R14K",
                 "FAST_CLEAR begin PC=0x%08lX R1=0x%08lX SP=0x%08lX bytes=%lu memsize=%lu",
                 (unsigned long)((uint32)PC),
                 (unsigned long)((uint32)R[1]),
                 (unsigned long)((uint32)SP),
                 (unsigned long)clear_bytes,
                 (unsigned long)((uint32)MEMSIZE));

        for (wi = 0; wi < clear_words; ++wi)
            M[wi] = 0;

        R[1] = 0;
        cc = CC_Z;
        PC = ROMBASE + 0x1F1Au;
        simhp4_r14k_done = 1;

        ESP_LOGI("SIMHP4_R14K",
                 "FAST_CLEAR done PC=0x%08lX R1=0x%08lX SP=0x%08lX CC=Z preserved_top=%lu",
                 (unsigned long)((uint32)PC),
                 (unsigned long)((uint32)R[1]),
                 (unsigned long)((uint32)SP),
                 (unsigned long)((uint32)MEMSIZE - clear_bytes));
        }
    }
#endif

    fault_PC = PC;
    recqptr = 0;                                        /* clr recovery q */
    AIO_CHECK_EVENT;                                    /* queue async events */
    if (sim_interval <= 0) {                            /* chk clock queue */
        temp = simhp4_r6r27a_process_event ();
        if (temp)
            ABORT (temp);
        SET_IRQL;                                       /* update interrupts */
#if defined(ESP_PLATFORM)
        simhp4_v13r10_host_yield();
#endif
        }

/* Test for non-instruction dispatches, in SRM order

        - trap or interrupt (trpirq != 0)
        - PSL<tp> set

   If any of these conditions are met, re-dispatch; otherwise,
   set PSL<tp> from PSL<t>.
*/

    if (trpirq) {                                       /* trap or interrupt? */
        if ((temp = GET_TRAP (trpirq))) {               /* trap? */
            cc = intexc (SCB_ARITH, cc, 0, IE_EXC);     /* take, clear trap */
            GET_CUR;                                    /* set cur mode */
            in_ie = 1;
            Write (SP - 4, temp, L_LONG, WA);           /* write parameter */
            SP = SP - 4;
            in_ie = 0;
            }
        else if ((temp = GET_IRQL (trpirq))) {          /* interrupt? */
            int32 vec;
            if (temp == IPL_HLTPIN) {                   /* console halt? */
                hlt_pin = 0;                            /* clear intr */
                trpirq = 0;                             /* clear everything */
                cc = con_halt (CON_HLTPIN, cc);         /* invoke firmware */
                continue;                               /* continue */
                }
            else if (temp >= IPL_HMIN)                  /* hardware req? */
                vec = get_vector (temp);                /* get vector */
            else if (temp > IPL_SMAX)
                ABORT (STOP_UIPL);
            else {
                vec = SCB_IPLSOFT + (temp << 2);
                SISR = SISR & ~(1u << temp);
                }
            if (vec)                                    /* take intr */
                cc = (++simhp4_r6r27a_irq_all, intexc (vec, cc, temp, IE_INT));
            GET_CUR;                                    /* set cur mode */
            }
        else trpirq = 0;                                /* clear everything */
        SET_IRQL;                                       /* eval interrupts */
        continue;
        }

    if (PSL & (PSL_CM|PSL_TP|PSW_T)) {                  /* PSL event? */
        if (PSL & PSL_TP) {                             /* trace trap? */
            PSL = PSL & ~PSL_TP;                        /* clear <tp> */
            cc = intexc (SCB_TP, cc, 0, IE_EXC);        /* take trap */
            GET_CUR;                                    /* set cur mode */
            continue;
            }
        if (PSL & PSW_T)                                /* if T, set TP */
            PSL = PSL | PSL_TP;
        if (PSL & PSL_CM) {                             /* compat mode? */
            cc = op_cmode (cc);                         /* exec instr */
            continue;                                   /* skip fetch */
            }
        }                                               /* end PSL event */

#if defined(ESP_PLATFORM)
    /* SIMHP4_R14W_QMR_EQUIV_FAST_FORWARD
     *
     * Exact KA630/VMB loop reconstructed from the shipped ka630.bin:
     *
     *   CLRL   R2
     * loop:
     *   TSTL   0x800(R1)[R2]
     *   BEQL   skip
     *   BISL3  #0x80000000,R2,0x800(R1)[R2]
     * skip:
     *   AOBLSS #0x2000,R2,loop
     *
     * At the observed VMB stage:
     *   PC = 0x0000081D  (the AOBLSS)
     *   R1 = 0x20087800
     *   0x800(R1)[R2] = 0x20088000 + 4*R2, i.e. KA630 Q22 map.
     *
     * The current R2 entry has already been processed before reaching
     * AOBLSS. Apply identical TSTL/BISL3 semantics to remaining entries,
     * then leave R2=0x1FFF and PC/CC untouched. The real guest AOBLSS
     * executes next, increments R2 to 0x2000, sets its real CC, and
     * naturally falls through.
     *
     * No QMR validity is invented:
     *   zero    -> remains zero
     *   nonzero -> exactly (0x80000000 | index), as guest BISL3 does.
     */
    {
        static int r14w_done = 0;

        if ((!r14w_done) &&
            ((uint32)PC == 0x0000081Du) &&
            ((uint32)R[0] == 0x00000001u) &&
            ((uint32)R[1] == 0x20087800u) &&
            ((uint32)R[2] <  0x00002000u) &&
            ((uint32)R[3] == 0x00000800u) &&
            ((uint32)SP   == 0x00004C00u)) {
            extern int32 qbmap_rd (int32 pa, int32 lnt);
            extern void qbmap_wr (int32 pa, int32 val, int32 lnt);

            const uint32 r14w_start = (uint32)R[2] + 1u;
            uint32 r14w_zero = 0;
            uint32 r14w_nonzero = 0;
            uint32 r14w_j;

            ESP_LOGI("SIMHP4_R14W",
                     "QMR_FAST_FORWARD begin PC=0x%08lX R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX SP=0x%08lX start=%lu end=8191",
                     (unsigned long)PC,
                     (unsigned long)R[0],
                     (unsigned long)R[1],
                     (unsigned long)R[2],
                     (unsigned long)R[3],
                     (unsigned long)SP,
                     (unsigned long)r14w_start);

            for (r14w_j = r14w_start; r14w_j < 0x2000u; ++r14w_j) {
                const int32 r14w_pa =
                    (int32)(0x20088000u + (r14w_j << 2));
                const int32 r14w_old =
                    qbmap_rd(r14w_pa, L_LONG);

                if (r14w_old != 0) {
                    qbmap_wr(r14w_pa,
                             (int32)(0x80000000u | r14w_j),
                             L_LONG);
                    ++r14w_nonzero;
                } else {
                    ++r14w_zero;
                }
            }

            R[2] = 0x00001FFFu;
            r14w_done = 1;

            ESP_LOGI("SIMHP4_R14W",
                     "QMR_FAST_FORWARD done processed=%lu nonzero=%lu zero=%lu R2=0x%08lX PC=0x%08lX guest_AOBLSS_finishes=1",
                     (unsigned long)(r14w_nonzero + r14w_zero),
                     (unsigned long)r14w_nonzero,
                     (unsigned long)r14w_zero,
                     (unsigned long)R[2],
                     (unsigned long)PC);
        }
    }
#endif

#if defined(ESP_PLATFORM)
    /* SIMHP4_R14X_BOOTRA_BSS_EQUIV_FAST_FORWARD
     *
     * 4.3BSD Quasijarus sys/vaxstand/srt0.c, REL path:
     *
     *      movl    $RELOC,sp
     *      movc3   aedata,entry,(sp)
     * dclr:
     *      clrl    (r3)+
     *      cmpl    r3,$_end
     *      jlss    dclr
     *
     * RELOC is 0x150000 for bootra.
     *
     * The observed low-memory VAX instruction sequence is verified at runtime:
     *   4C4A: D4 83                CLRL (R3)+
     *   4C4C: D1 53 8F xx xx xx xx CMPL R3,#_end
     *   4C53: 19 F5                JLSS 4C4A
     *
     * Parse _end from the actual guest instruction bytes.  Clear only the
     * remaining complete longwords BEFORE the final longword, set R3 to
     * (_end-4), and leave PC and CC untouched.  Guest then executes the final
     * CLRL/CMPL/JLSS itself, naturally producing R3==_end and the real CC.
     *
     * This is main RAM only; no device state, ROM, disk, or MSCP state changes.
     */
    {
        static int r14x_done = 0;

#define R14X_MEM8(_a) \
        ((uint32)((M[((uint32)(_a)) >> 2] >> ((((uint32)(_a)) & 3u) << 3)) & 0xFFu))

        if ((!r14x_done) &&
            ((uint32)PC == 0x00004C4Au) &&
            ((uint32)R[0] == 0x00000000u) &&
            ((uint32)R[2] == 0x00000000u) &&
            ((uint32)SP   == 0x00150000u) &&
            ((uint32)MEMSIZE > 0x00150000u) &&
            (R14X_MEM8(0x00004C4Au) == 0xD4u) &&
            (R14X_MEM8(0x00004C4Bu) == 0x83u) &&
            (R14X_MEM8(0x00004C4Cu) == 0xD1u) &&
            (R14X_MEM8(0x00004C4Du) == 0x53u) &&
            (R14X_MEM8(0x00004C4Eu) == 0x8Fu) &&
            (R14X_MEM8(0x00004C53u) == 0x19u) &&
            (R14X_MEM8(0x00004C54u) == 0xF5u)) {

            const uint32 r14x_end =
                (R14X_MEM8(0x00004C4Fu)      ) |
                (R14X_MEM8(0x00004C50u) <<  8) |
                (R14X_MEM8(0x00004C51u) << 16) |
                (R14X_MEM8(0x00004C52u) << 24);
            const uint32 r14x_start = (uint32)R[3];

            /* Strong state guard.  _end is parsed from the actual guest
             * CMPL immediate.  R1 is deliberately not used: it is not an
             * operand of this dclr loop, and physical R14X disproved the
             * previous R1==_end-RELOC assumption. */
            if ((r14x_end > 0x00150000u) &&
                (r14x_end <= (uint32)MEMSIZE) &&
                ((r14x_end & 3u) == 0u) &&
                (r14x_start >= 0x00150000u) &&
                (r14x_start < r14x_end) &&
                ((r14x_start & 3u) == 0u) &&
                /* SIMHP4_R14X_R2_REMOVE_FALSE_R1_GUARD
                 *
                 * R1 is not an operand of the dclr loop.  Physical R14X R1
                 * stayed 0x00006924, while the actual guest CMPL immediate
                 * parses _end=0x0016E834.  The former R1==_end-RELOC guard
                 * therefore rejected the real bootra loop.
                 *
                 * Keep the strong PC/SP/opcode/R3/_end guards above and
                 * remove only that unrelated R1 relation.
                 */
                ((r14x_end - r14x_start) >= 4u)) {

                const uint32 r14x_last = r14x_end - 4u;
                uint32 r14x_p;
                uint32 r14x_words = 0;

                ESP_LOGI("SIMHP4_R14X",
                         "BOOTRA_BSS_FAST_FORWARD begin PC=0x%08lX R1=0x%08lX R3=0x%08lX SP=0x%08lX parsed_end=0x%08lX",
                         (unsigned long)PC,
                         (unsigned long)R[1],
                         (unsigned long)R[3],
                         (unsigned long)SP,
                         (unsigned long)r14x_end);

                for (r14x_p = r14x_start; r14x_p < r14x_last; r14x_p += 4u) {
                    M[r14x_p >> 2] = 0;
                    ++r14x_words;
                }

                R[3] = r14x_last;
                r14x_done = 1;

                ESP_LOGI("SIMHP4_R14X",
                         "BOOTRA_BSS_FAST_FORWARD done host_words=%lu host_bytes=%lu R3=0x%08lX PC=0x%08lX guest_final_CLRL_CMPL_JLSS=1",
                         (unsigned long)r14x_words,
                         (unsigned long)(r14x_words * 4u),
                         (unsigned long)R[3],
                         (unsigned long)PC);
            }
        }

#undef R14X_MEM8
    }
#endif

#if defined(ESP_PLATFORM)
    /* SIMHP4_R14Y_R2_CONFIG_DELAY_EVENT_EQUIV_FAST_FORWARD
     *
     * Exact QUASDISK evidence:
     *   disk absolute 0x00000F9B:
     *       D0 8F 20 A1 07 00 5A    MOVL   #500000,R10
     *       F5 5A FD                SOBGTR R10,self
     *
     * LBN1 begins at disk offset 0x200 and bootra is relocated to 0x150000:
     *   0xF9B - 0x200 = 0xD9B
     *   0x150000 + 0xD9B = 0x00150D9B
     *
     * Therefore the physical blocker PC=0x00150DA2 is the SOBGTR inside
     * Quasijarus configure()'s DELAY(500000), not a device-ready poll.
     *
     * Preserve guest simulated time:
     *   - consume the skipped SOBGTR instruction count from sim_interval;
     *   - set only R10=1;
     *   - leave PC and condition codes untouched;
     *   - let the real guest SOBGTR execute the final iteration.
     *
     * Open SIMH's normal negative-sim_interval path then catches up all
     * overdue scheduled events before the following guest instruction.
     *
     * No device register, QMR, ROM, disk, MSCP state, or guest memory
     * contents are fabricated or changed here.
     */
    {
        static int r14y_r2_done = 0;

#define R14Y_R2_MEM8(_a) \
        ((uint32)((M[((uint32)(_a)) >> 2] >> ((((uint32)(_a)) & 3u) << 3)) & 0xFFu))

        if ((!r14y_r2_done) &&
            ((uint32)PC == 0x00150DA2u) &&
            ((uint32)R[0] == 0x00000008u) &&
            ((uint32)R[2] == 0x00000000u) &&
            ((uint32)SP   == 0x0014FFDCu) &&
            ((uint32)R[10] > 1u) &&
            ((uint32)R[10] <= 500000u) &&
            (R14Y_R2_MEM8(0x00150D9Bu) == 0xD0u) &&
            (R14Y_R2_MEM8(0x00150D9Cu) == 0x8Fu) &&
            (R14Y_R2_MEM8(0x00150D9Du) == 0x20u) &&
            (R14Y_R2_MEM8(0x00150D9Eu) == 0xA1u) &&
            (R14Y_R2_MEM8(0x00150D9Fu) == 0x07u) &&
            (R14Y_R2_MEM8(0x00150DA0u) == 0x00u) &&
            (R14Y_R2_MEM8(0x00150DA1u) == 0x5Au) &&
            (R14Y_R2_MEM8(0x00150DA2u) == 0xF5u) &&
            (R14Y_R2_MEM8(0x00150DA3u) == 0x5Au) &&
            (R14Y_R2_MEM8(0x00150DA4u) == 0xFDu)) {

            const uint32 r14y_r2_r10_before = (uint32)R[10];
            const uint32 r14y_r2_skipped = r14y_r2_r10_before - 1u;
            const int32 r14y_r2_interval_before = sim_interval;

            sim_interval -= (int32)r14y_r2_skipped;
            R[10] = 1u;
            r14y_r2_done = 1;

            ESP_LOGI("SIMHP4_R14Y_R2",
                     "CONFIG_DELAY_FAST_FORWARD PC=0x%08lX R10_before=%lu skipped=%lu sim_interval_before=%ld sim_interval_after_host=%ld R10_after=1 guest_final_SOBGTR=1 event_catchup_native=1",
                     (unsigned long)PC,
                     (unsigned long)r14y_r2_r10_before,
                     (unsigned long)r14y_r2_skipped,
                     (long)r14y_r2_interval_before,
                     (long)sim_interval);
        }

#undef R14Y_R2_MEM8
    }
#endif

#if defined(ESP_PLATFORM)
    /* SIMHP4_R14AA_KA630_TODR_DELAY_EQUIV_FAST_FORWARD
     *
     * R14Z proof:
     *   PC=0x8002FA5E, R11=0x00004E20 (20000), PSL/IPL=0
     *   VA 0x8002FA52..61:
     *     C5 8F 10 27 00 00 EF FB F1 02 00 5B F5 5B FD D6
     *   0x8002FA5E: F5 5B FD = SOBGTR R11,self
     *
     * Quasijarus VAX param.h:
     *   DELAY(n) { register int N = cpuspeed * (n); while (--N > 0); }
     * VAX630 machdep.c todr():
     *   { static int t; DELAY(10000); return (++t); }
     * R11=20000 therefore exactly matches cpuspeed=2 * 10000.
     *
     * Host-equivalent acceleration:
     *   validate exact live virtual code; transfer 19999 skipped SOBGTR
     *   instruction intervals to sim_interval; set only R11=1; leave PC/CC;
     *   guest executes final real SOBGTR and naturally reaches R11=0.
     * Native sim_process_event catches up expired scheduled events next dispatch.
     *
     * Repeatable for each todr() call. No device-ready state, guest memory,
     * device register, MSCP state, disk data, or todr() static increment is
     * fabricated here.
     */
    {
        static uint32 r14aa_hits = 0;

        if (((uint32)PC == 0x8002FA5Eu) &&
            ((uint32)R[11] == 0x00004E20u) &&
            mapen) {
            static const uint32 r14aa_expect[16] = {
                0xC5u, 0x8Fu, 0x10u, 0x27u, 0x00u, 0x00u, 0xEFu, 0xFBu,
                0xF1u, 0x02u, 0x00u, 0x5Bu, 0xF5u, 0x5Bu, 0xFDu, 0xD6u
            };
            int r14aa_match = 1;
            int i;

            for (i = 0; i < 16; ++i) {
                int32 st = -1;
                int32 pa = Test(0x8002FA52u + (uint32)i, RA, &st);
                if ((st != PR_OK) || (pa < 0) ||
                    !ADDR_IS_MEM((uint32)pa) ||
                    (((uint32)ReadB((uint32)pa) & 0xFFu) != r14aa_expect[i])) {
                    r14aa_match = 0;
                    break;
                }
            }

            if (r14aa_match) {
                const uint32 r14aa_r11_before = (uint32)R[11];
                const uint32 r14aa_skipped = r14aa_r11_before - 1u;
                const int32 r14aa_interval_before = sim_interval;

                sim_interval -= (int32)r14aa_skipped;
                R[11] = 1u;
                ++r14aa_hits;

                if ((r14aa_hits <= 8u) || ((r14aa_hits % 100u) == 0u)) {
                    ESP_LOGI("SIMHP4_R14AA",
                             "KA630_TODR_DELAY_FAST_FORWARD hit=%lu PC=0x%08lX R11_before=%lu skipped=%lu sim_interval_before=%ld sim_interval_after_host=%ld R11_after=1 guest_final_SOBGTR=1 event_catchup_native=1 source=DELAY_10000_cpuspeed2",
                             (unsigned long)r14aa_hits,
                             (unsigned long)PC,
                             (unsigned long)r14aa_r11_before,
                             (unsigned long)r14aa_skipped,
                             (long)r14aa_interval_before,
                             (long)sim_interval);
                }
            }
        }
    }
#endif

    if (sim_brk_summ &&
        sim_brk_test ((uint32) PC, SWMASK ('E'))) {     /* breakpoint? */
        ABORT (STOP_IBKPT);                             /* stop simulation */
        }

    sim_interval = sim_interval - (1 + (extra_bytes>>5));/* count instr */
    extra_bytes = 0;                                    /* digest string count */
    GET_ISTR (opc, L_BYTE);                             /* get opcode */
    if (opc == 0xFD) {                                  /* 2 byte op? */
        GET_ISTR (opc, L_BYTE);                         /* get second byte */
        opc = opc | 0x100;                              /* flag */
        }
    numspec = drom[opc][0];                             /* get # specs */
#if !defined(FULL_VAX)
    if (((DR_GETIGRP(numspec) == DR_GETIGRP(IG_BSDFL)) && (!(cpu_instruction_set & VAX_DFLOAT))) ||
        ((DR_GETIGRP(numspec) == DR_GETIGRP(IG_BSGFL)) && (!(cpu_instruction_set & VAX_GFLOAT))) ||
        (DR_GETIGRP(numspec) == DR_GETIGRP(IG_RSVD)))   /* explicit reserved instruction? */
        RSVD_INST_FAULT(opc);
#endif
#if defined(VAX_610)
    /* 
     * This case is formally UNPREDICTABLE, but it is how the MicroVAX I 
     * CPU worked.  Instructions without the DR_F in their drom table 
     * entry are specifically uninterruptible instructions, so this 
     * would not ever happen during normal execution, but the MicroVAX I
     * HCORE diagnostic contrives this as a test and expects thost cases
     * to be ignored.
     */
    if ((PSL & PSL_FPD) && (numspec & DR_F)) {
#else
    if (PSL & PSL_FPD) {
        if ((numspec & DR_F) == 0)
            RSVD_INST_FAULT(opc);
#endif
        j = 0;                                          /* no operands */
        }
    else {
        numspec = numspec & DR_NSPMASK;                 /* get # specifiers */

/* Specifier flows.  Operands are parsed and placed into queue opnd.

        r.bwl   opnd[j]         =       value of operand
        r.q     opnd[j:j+1]     =       value of operand
        r.o     opnd[j:j+3]     =       value of operand
        a.bwlqo opnd[j]         =       address of operand
        m.bwl   opnd[j]         =       value of operand
        m.q     opnd[j:j+1]     =       value of operand 
        m.o     opnd[j:j+3]     =       value of operand
        w.bwlqo opnd[j]         =       register/memory flag
                opnd[j+1]       =       memory address

   For the last memory specifier, the specifier is in spec, the register
   number is in rn, and the effective address is in va.  Modify specifiers
   (always last) can test spec > reg+PC, as short literal are illegal for
   modifiers specifiers, and final index specifiers are always illegal.
*/

        for (i = 1, j = 0; i <= numspec; i++) {         /* loop thru specs */
            disp = drom[opc][i];                        /* get dispatch */
            if (disp >= BB) {
                GET_ISTR (brdisp, DR_LNT (disp & 1));
                break;
                }
            GET_ISTR (spec, L_BYTE);                    /* get spec byte */
            rn = spec & RGMASK;                         /* get reg # */
            disp = (spec & ~RGMASK) | disp;             /* merge w dispatch */
            switch (disp) {                             /* dispatch spec */

/* Short literal - only read access permitted */

            case SH0|RB: case SH0|RW: case SH0|RL:
            case SH1|RB: case SH1|RW: case SH1|RL:
            case SH2|RB: case SH2|RW: case SH2|RL:
            case SH3|RB: case SH3|RW: case SH3|RL:
                opnd[j++] = spec;
                break;

            case SH0|RQ: case SH1|RQ: case SH2|RQ: case SH3|RQ:
                opnd[j++] = spec;
                opnd[j++] = 0;
                break;

            case SH0|RO: case SH1|RO: case SH2|RO: case SH3|RO:
                opnd[j++] = spec;
                opnd[j++] = 0;
                opnd[j++] = 0;
                opnd[j++] = 0;
                break;

            case SH0|RF: case SH1|RF: case SH2|RF: case SH3|RF:
                opnd[j++] = (spec << 4) | 0x4000;
                break;

            case SH0|RD: case SH1|RD: case SH2|RD: case SH3|RD:
                opnd[j++] = (spec << 4) | 0x4000;
                opnd[j++] = 0;
                break;

            case SH0|RG: case SH1|RG: case SH2|RG: case SH3|RG:
                opnd[j++] = (spec << 1) | 0x4000;
                opnd[j++] = 0;
                break;

            case SH0|RH: case SH1|RH: case SH2|RH: case SH3|RH:
                opnd[j++] = ((spec & 0x7) << 29) | (0x4000 | ((spec >> 3) & 0x7));
                opnd[j++] = 0;
                opnd[j++] = 0;
                opnd[j++] = 0;
                break;

/* Register */

            case GRN|RB: case GRN|MB:
                CHECK_FOR_PC;
                opnd[j++] = R[rn] & BMASK;
                break;

            case GRN|RW: case GRN|MW:
                CHECK_FOR_PC;
                opnd[j++] = R[rn] & WMASK;
                break;

            case GRN|VB:
                vfldrp1 = R[(rn + 1) & RGMASK];
            case GRN|WB: case GRN|WW: case GRN|WL: case GRN|WQ: case GRN|WO:
                opnd[j++] = rn;
            case GRN|RL: case GRN|RF: case GRN|ML:
                CHECK_FOR_PC;
                opnd[j++] = R[rn];
                break;

            case GRN|RQ: case GRN|RD: case GRN|RG: case GRN|MQ:
                CHECK_FOR_SP;
                opnd[j++] = R[rn];
                opnd[j++] = R[rn + 1];
                break;

            case GRN|RO: case GRN|RH: case GRN|MO:
                CHECK_FOR_AP;
                opnd[j++] = R[rn];
                opnd[j++] = R[rn + 1];
                opnd[j++] = R[rn + 2];
                opnd[j++] = R[rn + 3];
                break;

/*  Register deferred, autodecrement */

            case RGD|VB:
            case RGD|WB: case RGD|WW: case RGD|WL: case RGD|WQ: case RGD|WO:
                opnd[j++] = OP_MEM;
            case RGD|AB: case RGD|AW: case RGD|AL: case RGD|AQ: case RGD|AO:
                CHECK_FOR_PC;
                va = opnd[j++] = R[rn];
                break;

            case ADC|VB:
            case ADC|WB: case ADC|WW: case ADC|WL: case ADC|WQ: case ADC|WO:
                opnd[j++] = OP_MEM;
            case ADC|AB: case ADC|AW: case ADC|AL: case ADC|AQ: case ADC|AO:
                CHECK_FOR_PC;
                va = opnd[j++] = R[rn] = R[rn] - DR_LNT (disp);
                recq[recqptr++] = RQ_REC (disp, rn);
                break;

            case ADC|RB: case ADC|RW: case ADC|RL: case ADC|RF:
                R[rn] = R[rn] - (DR_LNT (disp));
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RB: case RGD|RW: case RGD|RL: case RGD|RF:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], DR_LNT (disp), RA);
                break;

            case ADC|RQ: case ADC|RD: case ADC|RG:
                R[rn] = R[rn] - 8;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RQ: case RGD|RD: case RGD|RG:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], L_LONG, RA);
                opnd[j++] = Read (R[rn] + 4, L_LONG, RA);
                break;

            case ADC|RO: case ADC|RH:
                R[rn] = R[rn] - 16;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RO: case RGD|RH:
                CHECK_FOR_PC;
                j = ReadOcta (va = R[rn], opnd, j, RA);
                break;

            case ADC|MB: case ADC|MW: case ADC|ML:
                R[rn] = R[rn] - (DR_LNT (disp));
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MB: case RGD|MW: case RGD|ML:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], DR_LNT (disp), WA);
                break;

            case ADC|MQ:
                R[rn] = R[rn] - 8;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MQ:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], L_LONG, WA);
                opnd[j++] = Read (R[rn] + 4, L_LONG, WA);
                break;

            case ADC|MO:
                R[rn] = R[rn] - 16;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MO:
                CHECK_FOR_PC;
                j = ReadOcta (va = R[rn], opnd, j, WA);
                break;

/* Autoincrement */

            case AIN|VB:
            case AIN|WB: case AIN|WW: case AIN|WL: case AIN|WQ: case AIN|WO:
                opnd[j++] = OP_MEM;
            case AIN|AB: case AIN|AW: case AIN|AL: case AIN|AQ: case AIN|AO:
                va = opnd[j++] = R[rn];
                if (rn == nPC) {
                    SETPC (PC + DR_LNT (disp));
                    }
                else {
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RB: case AIN|RW: case AIN|RL: case AIN|RF:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], DR_LNT (disp));
                    }
                else {
                    opnd[j++] = Read (R[rn], DR_LNT (disp), RA);
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RQ: case AIN|RD: case AIN|RG:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    opnd[j++] = Read (va, L_LONG, RA);
                    opnd[j++] = Read (va + 4, L_LONG, RA);  
                    R[rn] = R[rn] + 8;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RO: case AIN|RH:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    j = ReadOcta (va, opnd, j, RA);
                    R[rn] = R[rn] + 16;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MB: case AIN|MW: case AIN|ML:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], DR_LNT (disp));
                    }
                else {
                    opnd[j++] = Read (R[rn], DR_LNT (disp), WA);
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MQ:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    opnd[j++] = Read (va, L_LONG, WA);
                    opnd[j++] = Read (va + 4, L_LONG, WA);  
                    R[rn] = R[rn] + 8;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MO:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    j = ReadOcta (va, opnd, j, WA);
                    R[rn] = R[rn] + 16;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

/* Autoincrement deferred */

            case AID|VB:
            case AID|WB: case AID|WW: case AID|WL: case AID|WQ: case AID|WO:
                opnd[j++] = OP_MEM;
            case AID|AB: case AID|AW: case AID|AL: case AID|AQ: case AID|AO:
                if (rn == nPC) {
                    GET_ISTR (va = opnd[j++], L_LONG);
                    }
                else {
                    va = opnd[j++] = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                break;

            case AID|RB: case AID|RW: case AID|RL: case AID|RF:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case AID|RQ: case AID|RD: case AID|RG:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case AID|RO: case AID|RH:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                j = ReadOcta (va, opnd, j, RA);
                break;

            case AID|MB: case AID|MW: case AID|ML:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case AID|MQ:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case AID|MO:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Byte displacement */

            case BDP|VB:
            case BDP|WB: case BDP|WW: case BDP|WL: case BDP|WQ: case BDP|WO:
                opnd[j++] = OP_MEM;
            case BDP|AB: case BDP|AW: case BDP|AL: case BDP|AQ: case BDP|AO:
                GET_ISTR (temp, L_BYTE);
                va = opnd[j++] = R[rn] + SXTB (temp);
                break;

            case BDP|RB: case BDP|RW: case BDP|RL: case BDP|RF:
                GET_ISTR (temp, L_BYTE);
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case BDP|RQ: case BDP|RD: case BDP|RG:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case BDP|RO: case BDP|RH:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case BDP|MB: case BDP|MW: case BDP|ML:
                GET_ISTR (temp, L_BYTE);
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case BDP|MQ:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case BDP|MO:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Byte displacement deferred */

            case BDD|VB:
            case BDD|WB: case BDD|WW: case BDD|WL: case BDD|WQ: case BDD|WO:
                opnd[j++] = OP_MEM;
            case BDD|AB: case BDD|AW: case BDD|AL: case BDD|AQ: case BDD|AO:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case BDD|RB: case BDD|RW: case BDD|RL: case BDD|RF:
                GET_ISTR (temp, L_BYTE);        
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);    
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case BDD|RQ: case BDD|RD: case BDD|RG:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;  

            case BDD|RO: case BDD|RH:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;  

            case BDD|MB: case BDD|MW: case BDD|ML:
                GET_ISTR (temp, L_BYTE);        
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);    
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case BDD|MQ:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;  

            case BDD|MO:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;  

/* Word displacement */

            case WDP|VB:
            case WDP|WB: case WDP|WW: case WDP|WL: case WDP|WQ: case WDP|WO:
                opnd[j++] = OP_MEM;
            case WDP|AB: case WDP|AW: case WDP|AL: case WDP|AQ: case WDP|AO:
                GET_ISTR (temp, L_WORD);
                va = opnd[j++] = R[rn] + SXTW (temp);
                break;

            case WDP|RB: case WDP|RW: case WDP|RL: case WDP|RF:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case WDP|RQ: case WDP|RD: case WDP|RG:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case WDP|RO: case WDP|RH:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case WDP|MB: case WDP|MW: case WDP|ML:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case WDP|MQ:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case WDP|MO:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Word displacement deferred */

            case WDD|VB:
            case WDD|WB: case WDD|WW: case WDD|WL: case WDD|WQ: case WDD|WO:
                opnd[j++] = OP_MEM;
            case WDD|AB: case WDD|AW: case WDD|AL: case WDD|AQ: case WDD|AO:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case WDD|RB: case WDD|RW: case WDD|RL: case WDD|RF:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case WDD|RQ: case WDD|RD: case WDD|RG:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case WDD|RO: case WDD|RH:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case WDD|MB: case WDD|MW: case WDD|ML:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case WDD|MQ:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case WDD|MO:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Longword displacement */

            case LDP|VB:
            case LDP|WB: case LDP|WW: case LDP|WL: case LDP|WQ: case LDP|WO:
                opnd[j++] = OP_MEM;
            case LDP|AB: case LDP|AW: case LDP|AL: case LDP|AQ: case LDP|AO:
                GET_ISTR (temp, L_LONG);
                va = opnd[j++] = R[rn] + temp;
                break;

            case LDP|RB: case LDP|RW: case LDP|RL: case LDP|RF:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case LDP|RQ: case LDP|RD: case LDP|RG:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case LDP|RO: case LDP|RH:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                j = ReadOcta (va, opnd, j, RA);
                break;

            case LDP|MB: case LDP|MW: case LDP|ML:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case LDP|MQ:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case LDP|MO:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Longword displacement deferred */

            case LDD|VB:
            case LDD|WB: case LDD|WW: case LDD|WL: case LDD|WQ: case LDD|WO:
                opnd[j++] = OP_MEM;
            case LDD|AB: case LDD|AW: case LDD|AL: case LDD|AQ: case LDD|AO:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case LDD|RB: case LDD|RW: case LDD|RL: case LDD|RF:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case LDD|RQ: case LDD|RD: case LDD|RG:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case LDD|RO: case LDD|RH:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case LDD|MB: case LDD|MW: case LDD|ML:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case LDD|MQ:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case LDD|MO:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Index */

            case IDX|VB:
            case IDX|WB: case IDX|WW: case IDX|WL: case IDX|WQ: case IDX|WO:
            case IDX|AB: case IDX|AW: case IDX|AL: case IDX|AQ: case IDX|AO:
            case IDX|MB: case IDX|MW: case IDX|ML: case IDX|MQ: case IDX|MO:
            case IDX|RB: case IDX|RW: case IDX|RL: case IDX|RQ: case IDX|RO:
            case IDX|RF: case IDX|RD: case IDX|RG: case IDX|RH:
                CHECK_FOR_PC;
                index = R[rn] << (disp & DR_LNMASK);
                GET_ISTR (spec, L_BYTE);
                rn = spec & RGMASK;
                switch (spec & ~RGMASK) {
                case ADC:
                    R[rn] = R[rn] - DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (ADC | (disp & DR_LNMASK), rn);
                case RGD:
                    CHECK_FOR_PC;       
                    index = index + R[rn];
                    break;

                case AIN:
                    index = index + R[rn];
                    if (rn == nPC) {
                        IDX_IMM_TEST;
                        SETPC (PC + DR_LNT (disp));
                        }
                    else {
                        R[rn] = R[rn] + DR_LNT (disp);
                        recq[recqptr++] = RQ_REC (AIN | (disp & DR_LNMASK), rn);
                        }
                    break;

                case AID:
                    if (rn == nPC) {
                        GET_ISTR (temp, L_LONG);
                        }
                    else {
                        temp = Read (R[rn], L_LONG, RA);
                        R[rn] = R[rn] + 4;
                        recq[recqptr++] = RQ_REC (AID|RL, rn);
                        }
                    index = temp + index;
                    break;

                case BDP:
                    GET_ISTR (temp, L_BYTE);
                    index = index + R[rn] + SXTB (temp);
                    break;

                case BDD:
                    GET_ISTR (temp, L_BYTE);
                    index = index + Read (R[rn] + SXTB (temp), L_LONG, RA);
                    break;

                case WDP:
                    GET_ISTR (temp, L_WORD);
                    index = index + R[rn] + SXTW (temp);
                    break;

                case WDD:
                    GET_ISTR (temp, L_WORD);
                    index = index + Read (R[rn] + SXTW (temp), L_LONG, RA);
                    break;

                case LDP:
                    GET_ISTR (temp, L_LONG);
                    index = index + R[rn] + temp;
                    break;

                case LDD:
                    GET_ISTR (temp, L_LONG);
                    index = index + Read (R[rn] + temp, L_LONG, RA);
                    break;

                default:
                    RSVD_ADDR_FAULT;                    /* end case idxspec */
                    }

                switch (disp & (DR_ACMASK|DR_SPFLAG|DR_LNMASK)) { /* case acc+lnt */
                case VB:
                case WB: case WW: case WL: case WQ: case WO:
                    opnd[j++] = OP_MEM;
                case AB: case AW: case AL: case AQ: case AO:
                    va = opnd[j++] = index;
                    break;

                case RB: case RW: case RL: case RF:
                    opnd[j++] = Read (va = index, DR_LNT (disp), RA);
                    break;

                case RQ: case RD: case RG:
                    opnd[j++] = Read (va = index, L_LONG, RA);
                    opnd[j++] = Read (index + 4, L_LONG, RA);
                    break;

                case RO: case RH:
                    j = ReadOcta (va = index, opnd, j, RA);
                    break;

                case MB: case MW: case ML:
                    opnd[j++] = Read (va = index, DR_LNT (disp), WA);
                    break;

                case MQ:
                    opnd[j++] = Read (va = index, L_LONG, WA);
                    opnd[j++] = Read (index + 4, L_LONG, WA);
                    break;

                case MO:
                    j = ReadOcta (va = index, opnd, j, WA);
                    break;

                default:                                /* all others */
                    RSVD_ADDR_FAULT;                    /* fault */
                    break;
                    }                                   /* end case access/lnt */
                break;                                  /* end index */

            default:                                    /* all others */
                RSVD_ADDR_FAULT;                        /* fault */
                break;
                }                                       /* end case spec */
            }                                           /* end for */
        }                                               /* end if not FPD */

/* Optionally record instruction history */

    if (hst_lnt) {
        int32 lim;
        t_value wd;
        InstHistory *h = &hst[hst_p];

        h->iPC = fault_PC;
        h->PSL = PSL | cc;
        h->opc = opc;
        for (i = 0; i < j; i++)
            h->opnd[i] = opnd[i];
        lim = PC - fault_PC;
        if ((uint32) lim > INST_SIZE)
            lim = INST_SIZE;
        for (i = 0; i < lim; i++) {
            if ((cpu_ex (&wd, fault_PC + i, &cpu_unit, SWMASK ('V'))) == SCPE_OK)
                h->inst[i] = (uint8) wd;
            else {
                h->inst[0] = h->inst[1] = 0xFF;
                break;
                }
            }
        if (hst_switches & SWMASK('T'))
            h->time = sim_gtime();
        hst_p = hst_p + 1;
        if (hst_p >= hst_lnt)
            hst_p = 0;
        if (hst_log && (hst_p == hst_log_p))
            cpu_show_hist_records (hst_log, FALSE, hst_log_p, hst_lnt);
        }

/* Dispatch to instructions */

    switch (opc) {              

/* Single operand instructions with dest, write only - CLRx dst.wx

        spec    =       reg/memory flag
        rn      =       register number
        va      =       virtual address
*/

    case CLRB:
        r = 0;
        WRITE_B (r);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRW:
        r = 0;
        WRITE_W (r);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRL:
        r = 0;
        WRITE_L (r);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRQ:
        r = rh = 0;
        WRITE_Q (r, rh);                                /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

/* Single operand instructions with source, read only - TSTx src.rx

        opnd[0] =       source
*/

    case TSTB:
        CC_IIZZ_B (op0);                                /* set cc's */
        break;

    case TSTW:
        CC_IIZZ_W (op0);                                /* set cc's */
        break;

    case TSTL:
        CC_IIZZ_L (op0);                                /* set cc's */
        if ((cc == CC_Z) &&                             /* zero result and */
            ((PC - fault_PC) == 6) &&                   /* 6 byte instruction? */
            (fault_PC & 0x80000000) &&                  /* in system space? */
            (((cpu_idle_mask & VAX_IDLE_VMS) &&         /* VMS 5.0 and 5.1 */
              (PSL_GETIPL (PSL) == 0x3) &&              /*  at IPL 3 */
              (PSL_IS & PSL)) ||                        /*  on the interrupt stack */
             ((((cpu_idle_mask & VAX_IDLE_ULTOLD) &&    /* running Old Ultrix or friends? */
               (PSL_GETIPL (PSL) == 0x1)) ||            /*  at IPL 1? */
              ((cpu_idle_mask & VAX_IDLE_QUAD) &&       /* running Quasijarus or friends? */
               (PSL_GETIPL (PSL) == 0x0))) &&           /*  at IPL 0? */
              ((fault_PC & 0x7fffffff) < 0x4000))))     /* in low system space? */
            cpu_idle();                                 /* idle loop */
        break;

/* Single operand instructions with source, read/write - op src.mx

        opnd[0] =       operand
        spec    =       reg/mem flag
        rn      =       register number
        va      =       operand address
*/

    case INCB:
        r = (op0 + 1) & BMASK;                          /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_ADD_B (r, 1, op0);                           /* set cc's */
        break;

    case INCW:
        r = (op0 + 1) & WMASK;                          /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_ADD_W (r, 1, op0);                           /* set cc's */
        break;

    case INCL:
        r = (op0 + 1) & LMASK;                          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, 1, op0);                           /* set cc's */
        break;

    case DECB:
        r = (op0 - 1) & BMASK;                          /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, 1, op0);                           /* set cc's */
        break;

    case DECW:
        r = (op0 - 1) & WMASK;                          /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, 1, op0);                           /* set cc's */
        break;

    case DECL:
        r = (op0 - 1) & LMASK;                          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, 1, op0);                           /* set cc's */
        break;

/* Push instructions - PUSHL src.rl or PUSHAx src.ax
        
        opnd[0] =       source
*/

    case PUSHL: case PUSHAB: case PUSHAW: case PUSHAL: case PUSHAQ:
        Write (SP - 4, op0, L_LONG, WA);                /* push operand */
        SP = SP - 4;                                    /* decr stack ptr */
        CC_IIZP_L (op0);                                /* set cc's */
        break;

/* Moves, converts, and ADAWI - op src.rx, dst.wx
        
        opnd[0] =       source
        spec    =       reg/mem flag
        rn      =       register number
        va      =       operand address
*/

    case MOVB:
        r = op0;
        WRITE_B (r);                                    /* result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case MOVW: case MOVZBW:
        r = op0;
        WRITE_W (r);                                    /* result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case MOVL: case MOVZBL: case MOVZWL:
    case MOVAB: case MOVAW: case MOVAL: case MOVAQ:
        r = op0;
        WRITE_L (r);                                    /* result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case MCOMB:
        r = op0 ^ BMASK;                                /* compl opnd */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case MCOMW:
        r = op0 ^ WMASK;                                /* compl opnd */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case MCOML:
        r = op0 ^ LMASK;                                /* compl opnd */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case MNEGB:
        r = (-op0) & BMASK;                             /* negate opnd */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, op0, 0);                           /* set cc's */
        break;

    case MNEGW:
        r = (-op0) & WMASK;                             /* negate opnd */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, op0, 0);                           /* set cc's */
        break;

    case MNEGL:
        r = (-op0) & LMASK;                             /* negate opnd */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, 0);                           /* set cc's */
        break;

    case CVTBW:
        r = SXTBW (op0);                                /* ext sign */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* set cc's */
        break;

    case CVTBL:
        r = SXTB (op0);                                 /* ext sign */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        break;

    case CVTWL:
        r = SXTW (op0);                                 /* ext sign */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        break;

    case CVTLB:
        r = op0 & BMASK;                                /* set result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* initial cc's */
        if ((op0 > 127) || (op0 < -128)) {
            V_INTOV;
            }
        break;

    case CVTLW:
        r = op0 & WMASK;                                /* set result */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* initial cc's */
        if ((op0 > 32767) || (op0 < -32768)) {
            V_INTOV;
            }
        break;

    case CVTWB:
        r = op0 & BMASK;                                /* set result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* initial cc's */
        temp = SXTW (op0);                              /* cvt op to long */
        if ((temp > 127) || (temp < -128)) {
            V_INTOV;
            }
        break;

    case ADAWI:
        if (op1 >= 0) temp = R[op1] & WMASK;            /* reg? ADDW2 */
        else {
            if (op2 & 1)                                /* mem? chk align */
                RSVD_OPND_FAULT(ADAWI);
            temp = Read (op2, L_WORD, WA);              /* ok, ADDW2 */
            }
        r = (op0 + temp) & WMASK;
        WRITE_W (r);
        CC_ADD_W (r, op0, temp);                        /* set cc's */
        break;

/* Integer operates, 2 operand, read only - op src1.rx, src2.rx

        opnd[0] =       source1
        opnd[1] =       source2
*/

    case CMPB:
        CC_CMP_B (op0, op1);                            /* set cc's */
        break;

    case CMPW:
        CC_CMP_W (op0, op1);                            /* set cc's */
        break;

    case CMPL:
        CC_CMP_L (op0, op1);                            /* set cc's */
        break;

    case BITB:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BITW:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BITL:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_L (r);                                  /* set cc's */
        if ((cc == CC_Z) &&
            (cpu_idle_mask & VAX_IDLE_ULT) &&           /* running Ultrix or friends? */
            ((PSL & PSL_IS) != 0) &&                    /* on IS? */
            (PSL_GETIPL (PSL) == 0x18) &&               /* at IPL 18? */
            (fault_PC & 0x80000000) &&                  /* in system space? */
            ((PC - fault_PC) == 8) &&                   /* 8 byte instruction? */
            ((fault_PC & 0x7fffffff) < 0x6000))         /* in low system space? */
            cpu_idle();                                 /* idle loop */
        break;

/* Integer operates, 2 operand read/write, and 3 operand, also MOVQ
        op2 src.rx, dst.mx      op3 src.rx, src.rx, dst.wx

        opnd[0] =       source1
        opnd[1] =       source2
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ADDB2: case ADDB3:
        r = (op1 + op0) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_ADD_B (r, op0, op1);                         /* set cc's */
        break;

    case ADDW2: case ADDW3:
        r = (op1 + op0) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_ADD_W (r, op0, op1);                         /* set cc's */
        break;

    case ADWC:
        r = (op1 + op0 + (cc & CC_C)) & LMASK;          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, op0, op1);                         /* set cc's */
        if ((r == op1) && op0)                          /* special case */
            cc = cc | CC_C;
        break;

    case ADDL2: case ADDL3:
        r = (op1 + op0) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, op0, op1);                         /* set cc's */
        break;

    case SUBB2: case SUBB3:
        r = (op1 - op0) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, op0, op1);                         /* set cc's */
        break;

    case SUBW2: case SUBW3:
        r = (op1 - op0) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, op0, op1);                         /* set cc's */
        break;

    case SBWC:
        r = (op1 - op0 - (cc & CC_C)) & LMASK;          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, op1);                         /* set cc's */
        if ((op0 == op1) && r)                          /* special case */
            cc = cc | CC_C;
        break;

    case SUBL2: case SUBL3:
        r = (op1 - op0) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, op1);                         /* set cc's */
        break;

    case MULB2: case MULB3:
        temp = SXTB (op0) * SXTB (op1);                 /* multiply */
        r = temp & BMASK;                               /* mask to result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* set cc's */
        if ((temp > 127) || (temp < -128)) {
            V_INTOV;
            }
        break;

    case MULW2: case MULW3:
        temp = SXTW (op0) * SXTW (op1);                 /* multiply */
        r = temp & WMASK;                               /* mask to result */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* set cc's */
        if ((temp > 32767) || (temp < -32768)) {
            V_INTOV;
            }
        break;

    case MULL2: case MULL3:
        r = op_emul (op0, op1, &rh);                    /* get 64b result */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        if (rh != ((r & LSIGN)? -1: 0)) {               /* chk overflow */
            V_INTOV;
            }
        break;

    case DIVB2: case DIVB3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((op0 == BMASK) && (op1 == BSIGN)) {    /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = SXTB (op1) / SXTB (op0);                /* ok, divide */
            temp = 0;
            }
        r = r & BMASK;                                  /* mask to result */
        WRITE_B (r);                                    /* write result */
        CC_IIZZ_B (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case DIVW2: case DIVW3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((op0 == WMASK) && (op1 == WSIGN)) {    /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = SXTW (op1) / SXTW (op0);                /* ok, divide */
            temp = 0;
            }
        r = r & WMASK;                                  /* mask to result */
        WRITE_W (r);                                    /* write result */
        CC_IIZZ_W (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case DIVL2: case DIVL3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((((uint32)op0) == LMASK) && 
                 (((uint32)op1) == LSIGN)) {            /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = op1 / op0;                              /* ok, divide */
            temp = 0;
            }
        r = r & LMASK;                                  /* mask to result */
        WRITE_L (r);                                    /* write result */
        CC_IIZZ_L (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case BISB2: case BISB3:
        r = op1 | op0;                                  /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BISW2: case BISW3:
        r = op1 | op0;                                  /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BISL2: case BISL3:
        r = op1 | op0;                                  /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case BICB2: case BICB3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BICW2: case BICW3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BICL2: case BICL3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case XORB2: case XORB3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case XORW2: case XORW3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case XORL2: case XORL3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

/* MOVQ - movq src.rq, dst.wq

        opnd[0:1] =     source
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
        
*/

    case MOVQ:
        WRITE_Q (op0, op1);                             /* store result */
        CC_IIZP_Q (op0, op1);
        break;

/* Shifts - op shf.rb,src.rl,dst.wl

        opnd[0] =       shift count
        opnd[1] =       source
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ROTL:
        j = op0 % 32;                                   /* reduce sc, mod 32 */
        if (j)
            r = ((((uint32) op1) << j) | (((uint32) op1) >> (32 - j))) & LMASK;
        else r = op1;
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case ASHL:
        if (op0 & BSIGN) {                              /* right shift? */
            temp = 0x100 - op0;                         /* get |shift| */
            if (temp > 31)                              /* sc > 31? */
                r = (op1 & LSIGN)? LMASK: 0;
            else r = op1 >> temp;                       /* shift */
            WRITE_L (r);                                /* store result */
            CC_IIZZ_L (r);                              /* set cc's */
            break;
            }
        else {
            if (op0 > 31)                               /* sc > 31? */
                r = temp = 0;
            else {
                r = (((uint32) op1) << op0) & LMASK;    /* shift */
                temp = r >> op0;                        /* shift back */
                }
            WRITE_L (r);                                /* store result */
            CC_IIZZ_L (r);                              /* set cc's */
            if (op1 != temp) {                          /* bits lost? */
                V_INTOV;
                }
            }
        break;

    case ASHQ:
        r = op_ashq (opnd, &rh, &flg);                  /* do qw shift */
        WRITE_Q (r, rh);                                /* store results */
        CC_IIZZ_Q (r, rh);                              /* set cc's */
        if (flg) {                                      /* if ovflo, set */
            V_INTOV;
            }
        break;

/* EMUL - emul mplr.rl,mpcn.rl,add.rl,dst.wq

        op0     =       multiplier
        op1     =       multiplicand
        op2     =       adder
        op3:op4 =       destination (.wq)
*/

    case EMUL:
        r = op_emul (op0, op1, &rh);                    /* calc 64b result */
        r = r + op2;                                    /* add 32b value */
        rh = rh + (((uint32) r) < ((uint32) op2)) -     /* into 64b result */
            ((op2 & LSIGN)? 1: 0);
        WRITE_Q (r, rh);                                /* write result */
        CC_IIZZ_Q (r, rh);                              /* set cc's */
        break;

/* EDIV - ediv dvr.rl,dvd.rq,quo.wl,rem.wl

        op0     =       divisor (.rl)
        op1:op2 =       dividend (.rq)
        op3:op4 =       quotient address (.wl)
        op5:op6 =       remainder address (.wl)
*/

    case EDIV:
        if (op5 < 0)                                    /* wtest remainder */
            Read (op6, L_LONG, WA);
        if (op0 == 0) {                                 /* divide by zero? */
            flg = CC_V;                                 /* set V */
            r = opnd[1];                                /* quo = low divd */
            rh = 0;                                     /* rem = 0 */
            SET_TRAP (TRAP_DIVZRO);                     /* set trap */
            }
        else {
            r = op_ediv (opnd, &rh, &flg);              /* extended divide */
            if (flg) {                                  /* if ovf+IV, set trap */
                INTOV;
                }
            }
        if (op3 >= 0)                                   /* store quotient */
            R[op3] = r;
        else Write (op4, r, L_LONG, WA);
        if (op5 >= 0)                                   /* store remainder */
            R[op5] = rh;
        else Write (op6, rh, L_LONG, WA);
        CC_IIZZ_L (r);                                  /* set cc's */
        cc = cc | flg;                                  /* set V if required */
        break;

/* Control instructions */

/* Simple branches and subroutine calls */

    case BRB:
        BRANCHB (brdisp);                               /* branch  */
        break;

    case BRW:
        BRANCHW (brdisp);                               /* branch */
        break;

    case BSBB:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */
        BRANCHB (brdisp);                               /* branch  */
        if (sim_switches & SWMASK ('R'))
            ++step_out_nest_level;
        break;

    case BSBW:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */
        BRANCHW (brdisp);                               /* branch */
        if (sim_switches & SWMASK ('R'))
            ++step_out_nest_level;
        break;

    case BGEQ:
        if (!(cc & CC_N))                               /* br if N = 0 */
            BRANCHB (brdisp);
        break;

    case BLSS:
        if (cc & CC_N)                                  /* br if N = 1 */
            BRANCHB (brdisp);
        break;

    case BNEQ:
        if (!(cc & CC_Z))                               /* br if Z = 0 */
            BRANCHB (brdisp);
        break;

    case BEQL:
        if (cc & CC_Z) {                                /* br if Z = 1 */
            BRANCHB (brdisp);
            if ((((PSL & PSL_IS) != 0) &&               /* on IS? */
                 (PSL_GETIPL (PSL) == 0x1F) &&          /* at IPL 31 */
                 (mapen == 0) &&                        /* Running from ROM */
                 ((fault_PC == 0x2004361B) ||           /* at a */
                  (fault_PC == 0x20046A36))) ||         /* Boot ROM Character Prompt */
                ((cpu_idle_mask & VAX_IDLE_ELN) &&      /* VAXELN Idle? */
                 (PSL & PSL_IS) &&                      /* on IS? */
                 (brdisp == 0xFA) &&                    /* Branch to prior TSTL */
                 (PSL_GETIPL (PSL) == 0x4)))            /* at IPL 4 */
                cpu_idle();
            }
        break;

    case BVC:
        if (!(cc & CC_V))                               /* br if V = 0 */
            BRANCHB (brdisp);
        break;

    case BVS:
        if (cc & CC_V) {                                /* br if V = 1 */
            BRANCHB (brdisp);
            if ((cpu_idle_mask & VAX_IDLE_INFOSERVER) &&/* INFOSERVER Idle? */
                 (PSL & PSL_IS) &&                      /* on IS? */
                 (brdisp == 0xF1) &&                    /* Branch to prior INCL */
                 (PSL_GETIPL (PSL) == 0x3))             /* at IPL 3 */
                cpu_idle();
            }
        break;

    case BGEQU:
        if (!(cc & CC_C))                               /* br if C = 0 */
            BRANCHB (brdisp);
        break;

    case BLSSU:
        if (cc & CC_C)                                  /* br if C = 1 */
            BRANCHB (brdisp);
        break;

    case BGTR:
        if (!(cc & (CC_N | CC_Z)))                      /* br if N | Z = 0 */
            BRANCHB (brdisp);
        break;

    case BLEQ:
        if (cc & (CC_N | CC_Z))                         /* br if N | Z = 1 */
            BRANCHB (brdisp);
        break;

    case BGTRU:
        if (!(cc & (CC_C | CC_Z)))                      /* br if C | Z = 0 */
            BRANCHB (brdisp);
        break;

    case BLEQU:
        if (cc & (CC_C | CC_Z))                         /* br if C | Z = 1 */
            BRANCHB (brdisp);
        break;

/* Simple jumps and subroutine calls - op addr.ab

        opnd[0] =       address
*/

    case JSB:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */
        if (sim_switches & SWMASK ('R'))
            ++step_out_nest_level;

    case JMP:
        JUMP (op0);                                     /* jump */
        break;

    case RSB:
        temp = Read (SP, L_LONG, RA);                   /* get top of stk */
        SP = SP + 4;                                    /* incr stk ptr */
        JUMP_ALWAYS (temp);
        if (sim_switches & SWMASK ('R')) {
            if (step_out_nest_level <= 0)
                ABORT (SCPE_STEP);
            else
                --step_out_nest_level;
            }
        break;

/* SOB instructions - op idx.ml,disp.bb

        opnd[0] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case SOBGEQ:
        r = op0 - 1;                                    /* decr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_SUB_L (r, 1, op0);                            /* test for ovflo */    
        if (r >= 0)                                     /* if >= 0, branch */
            BRANCHB_ALWAYS (brdisp);
        break;

    case SOBGTR:
        r = op0 - 1;                                    /* decr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_SUB_L (r, 1, op0);                            /* test for ovflo */    
        if (r > 0)                                      /* if >= 0, branch */
            BRANCHB_ALWAYS (brdisp);
        break;

/* AOB instructions - op limit.rl,idx.ml,disp.bb

        opnd[0] =       limit
        opnd[1] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case AOBLSS:
        r = op1 + 1;                                    /* incr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, 1, op1);                            /* test for ovflo */
        if (r < op0)                                    /* if < lim, branch */
            BRANCHB_ALWAYS (brdisp);
        break;

    case AOBLEQ:
        r = op1 + 1;                                    /* incr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, 1, op1);                            /* test for ovflo */
        if (r <= op0)                                   /* if < lim, branch */
            BRANCHB_ALWAYS (brdisp);
        break;

/* ACB instructions - op limit.rx,add.rx,index.mx,disp.bw

        opnd[0] =       limit
        opnd[1] =       adder
        opnd[2] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ACBB:
        r = (op2 + op1) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        V_ADD_B (r, op1, op2);                          /* test for ovflo */
        if ((op1 & BSIGN)? (SXTB (r) >= SXTB (op0)): (SXTB (r) <= SXTB (op0)))
            BRANCHW_ALWAYS (brdisp);
        break;

    case ACBW:
        r = (op2 + op1) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        V_ADD_W (r, op1, op2);                          /* test for ovflo */
        if ((op1 & WSIGN)? (SXTW (r) >= SXTW (op0)): (SXTW (r) <= SXTW (op0)))
            BRANCHW_ALWAYS (brdisp);
        break;

    case ACBL:
        r = (op2 + op1) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, op1, op2);                          /* test for ovflo */
        if ((op1 & LSIGN)? (r >= op0): (r <= op0))
            BRANCHW_ALWAYS (brdisp);
        break;

/* CASE instructions - casex sel.rx,base.rx,lim.rx

        opnd[0] =       selector
        opnd[1] =       base
        opnd[2] =       limit
*/

    case CASEB:
        r = (op0 - op1) & BMASK;                        /* sel - base */
        CC_CMP_B (r, op2);                              /* r:limit, set cc's */
        if (r > op2)                                    /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

    case CASEW:
        r = (op0 - op1) & WMASK;                        /* sel - base */
        CC_CMP_W (r, op2);                              /* r:limit, set cc's */
        if (r > op2)                                    /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

    case CASEL:
        r = (op0 - op1) & LMASK;                        /* sel - base */
        CC_CMP_L (r, op2);                              /* r:limit, set cc's */
        if (((uint32) r) > ((uint32) op2))              /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

/* Branch on bit instructions - bbxy pos.rl,op.wb,disp.bb

        opnd[0] =       position
        opnd[1] =       register number/memory flag
        opnd[2] =       memory address, if memory
*/

    case BBS:
        if (op_bb_n (opnd, acc)) {                      /* br if bit set */
            BRANCHB_ALWAYS (brdisp);
            if (((PSL & PSL_IS) != 0) &&                /* on IS? */
                (PSL_GETIPL (PSL) == 0x3) &&            /* at IPL 3? */
                ((cpu_idle_mask & VAX_IDLE_VMS) != 0))  /* running VMS? */
                cpu_idle ();                            /* idle loop */
            }
        break;

    case BBC:
        if (!op_bb_n (opnd, acc))                       /* br if bit clr */
            BRANCHB_ALWAYS (brdisp);
        break;

    case BBSS: case BBSSI:
        if (op_bb_x (opnd, 1, acc))                     /* br if set, set */
            BRANCHB (brdisp);
        break;

    case BBCC: case BBCCI:
        if (!op_bb_x (opnd, 0, acc))                    /* br if clr, clr*/
            BRANCHB (brdisp);
        break;

    case BBSC:
        if (op_bb_x (opnd, 0, acc))                     /* br if clr, set */
            BRANCHB_ALWAYS (brdisp);
        break;

    case BBCS:
        if (!op_bb_x (opnd, 1, acc))                    /* br if set, clr */
            BRANCHB_ALWAYS (brdisp);
        break;

    case BLBS:
        if (op0 & 1)                                    /* br if bit set */
            BRANCHB (brdisp);
        break;

    case BLBC:
        if ((op0 & 1) == 0) {                           /* br if bit clear */
            if (fault_PC == 0x20040C09)                 /* MicroVAX 2 Boot ROM Character Prompt? */
                cpu_idle();
            BRANCHB (brdisp);
            }
        break;

/* Extract field instructions - ext?v pos.rl,size.rb,base.wb,dst.wl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case EXTV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        if (r & byte_sign[op1])
            r = r | ~byte_mask[op1];
        WRITE_L (r);                                    /* store field */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case EXTZV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        WRITE_L (r);                                    /* store field */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

/* Compare field instructions - cmp?v pos.rl,size.rb,base.wb,src2.rl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        opnd[4] =       source2
*/

    case CMPV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        if (r & byte_sign[op1])
            r = r | ~byte_mask[op1];
        CC_CMP_L (r, op4);                              /* set cc's */
        break;

    case CMPZV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        CC_CMP_L (r, op4);                              /* set cc's */
        break;

/* Find first field instructions - ff? pos.rl,size.rb,base.wb,dst.wl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case FFS:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        temp = op_ffs (r, op1);                         /* find first 1 */
        WRITE_L (op0 + temp);                           /* store result */
        cc = r? 0: CC_Z;                                /* set cc's */
        r = op0 + temp;
        if ((cc == CC_Z) &&                             /* No set bits found? */
            (cpu_idle_mask & VAX_IDLE_ULT1X) &&         /* running Ultrix 1.X" */
            (PSL_GETIPL (PSL) == 0x0) &&                /*  at IPL 0? */
            (fault_PC & 0x80000000) &&                  /*  in system space? */
            ((fault_PC & 0x7fffffff) < 0x3000))         /*  in low system space? */
            cpu_idle();                                 /* idle loop */
        break;

    case FFC:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        r = r ^ byte_mask[op1];                         /* invert bits */
        temp = op_ffs (r, op1);                         /* find first 1 */
        WRITE_L (op0 + temp);                           /* store result */
        cc = r? 0: CC_Z;                                /* set cc's */
        r = op0 + temp;
        break;

/* Insert field instruction - insv src.rl,pos.rb,size.rl,base.wb

        opnd[0] =       source
        opnd[1] =       position
        opnd[2] =       size
        opnd[3] =       register number/memory flag
        opnd[4] =       register content/memory address
*/

    case INSV:
        op_insv (opnd, vfldrp1, acc);                   /* insert field */
        break;

/* Call and return - call? arg.rx,proc.ab

        opnd[0] =       argument
        opnd[1] =       procedure address
*/

    case CALLS:
        cc = op_call (opnd, TRUE, acc);
        if (sim_switches & SWMASK ('R'))
            ++step_out_nest_level;
        break;

    case CALLG:
        cc = op_call (opnd, FALSE, acc);
        if (sim_switches & SWMASK ('R'))
            ++step_out_nest_level;
        break;

    case RET:
        cc = op_ret (acc);
        if (sim_switches & SWMASK ('R')) {
            if (step_out_nest_level <= 0)
                ABORT (SCPE_STEP);
            else
                --step_out_nest_level;
            }
        break;

/* Miscellaneous instructions */

    case HALT:
        if (PSL & PSL_CUR)                              /* not kern? rsvd inst */
            RSVD_INST_FAULT(HALT);
        else {
            /* allow potentially pending I/O (console output, 
               or other devices) to complete before taking
               the appropriate halt action */
            while ((sim_clock_queue != QUEUE_LIST_END) &&
                   ((sim_clock_queue->flags & UNIT_IDLE) == 0)) {
                sim_interval = 0;
                temp = sim_process_event ();
                if (temp)
                    ABORT (temp);
                SET_IRQL;                               /* update interrupts */
                }
            if (cpu_unit.flags & UNIT_CONH)             /* halt to console? */
                cc = con_halt (CON_HLTINS, cc);         /* enter firmware */
            else
                ABORT (STOP_HALT);                      /* halt to simulator */
            }

    case NOP:
        break;

    case BPT:
        SETPC (fault_PC);
        PSL = PSL & ~PSL_TP;                                /* clear <tp> */
        cc = intexc (SCB_BPT, cc, 0, IE_EXC);
        GET_CUR;
        break;

    case XFC:
        SETPC (fault_PC);
        PSL = PSL & ~PSL_TP;                                /* clear <tp> */
        cc = intexc (SCB_XFC, cc, 0, IE_EXC);
        GET_CUR;
        break;

    case BISPSW:
        if (opnd[0] & PSW_MBZ)
            RSVD_OPND_FAULT(BISPW);
        PSL = PSL | (opnd[0] & ~CC_MASK);
        cc = cc | (opnd[0] & CC_MASK);
        break;

    case BICPSW:
        if (opnd[0] & PSW_MBZ)
            RSVD_OPND_FAULT(BICPSW);
        PSL = PSL & ~opnd[0];
        cc = cc & ~opnd[0];
        break;

    case MOVPSL:
        r = PSL | cc;
        WRITE_L (r);
        break;

    case PUSHR:
        op_pushr (opnd, acc);
        break;

    case POPR:
        op_popr (opnd, acc);
        break;

    case INDEX:
        if ((op0 < op1) || (op0 > op2))
            SET_TRAP (TRAP_SUBSCR);
        r = (op0 + op4) * op3;
        WRITE_L (r);
        CC_IIZZ_L (r);
        break;

/* Queue and interlocked queue */

    case INSQUE:
        cc = op_insque (opnd, acc);
        break;

    case REMQUE:
        cc = op_remque (opnd, acc);
        break;

    case INSQHI:
        cc = op_insqhi (opnd, acc);
        break;

    case INSQTI:
        cc = op_insqti (opnd, acc);
        break;

    case REMQHI:
        cc = op_remqhi (opnd, acc);
        break;

    case REMQTI:
        cc = op_remqti (opnd, acc);
        break;

/* String instructions */

    case MOVC3: case MOVC5:
        cc = op_movc (opnd, opc & 4, acc);
        break;

    case CMPC3: case CMPC5:
#if defined(VAX_610)
        if (opc == CMPC5) {
            cc = cpu_emulate_exception (opnd, cc, opc, acc);
            break;
            }
#endif
        cc = op_cmpc (opnd, opc & 4, acc);
        break;

    case LOCC: case SKPC:
        cc = op_locskp (opnd, opc & 1, acc);
        break;

    case SCANC: case SPANC:
        cc = op_scnspn (opnd, opc & 1, acc);
        break;

/* Floating point instructions */

    case TSTF: case TSTD:
        r = op_movfd (op0);
        CC_IIZZ_FP (r);
        break;

    case TSTG:
        r = op_movg (op0);
        CC_IIZZ_FP (r);
        break;

    case MOVF:
        r = op_movfd (op0);
        WRITE_L (r);
        CC_IIZP_FP (r);
        break;

    case MOVD:
        if ((r = op_movfd (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZP_FP (r);
        break;

    case MOVG:
        if ((r = op_movg (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZP_FP (r);
        break;

    case MNEGF:
        r = op_mnegfd (op0);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case MNEGD:
        if ((r = op_mnegfd (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZZ_FP (r);
        break;

    case MNEGG:
        if ((r = op_mnegg (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZZ_FP (r);
        break;

    case CMPF:
        cc = op_cmpfd (op0, 0, op1, 0);
        break;

    case CMPD:
        cc = op_cmpfd (op0, op1, op2, op3);
        break;

    case CMPG:
        cc = op_cmpg (op0, op1, op2, op3);
        break;

    case CVTBF:
        r = op_cvtifdg (SXTB (op0), NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTWF:
        r = op_cvtifdg (SXTW (op0), NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTLF:
        r = op_cvtifdg (op0, NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTBD: case CVTBG:
        r = op_cvtifdg (SXTB (op0), &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTWD: case CVTWG:
        r = op_cvtifdg (SXTW (op0), &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTLD: case CVTLG:
        r = op_cvtifdg (op0, &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTFB: case CVTDB: case CVTGB:
        r = op_cvtfdgi (opnd, &flg, opc) & BMASK;
        WRITE_B (r);
        CC_IIZZ_B (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFW: case CVTDW: case CVTGW:
        r = op_cvtfdgi (opnd, &flg, opc) & WMASK;
        WRITE_W (r);
        CC_IIZZ_W (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFL: case CVTDL: case CVTGL:
    case CVTRFL: case CVTRDL: case CVTRGL:
        r = op_cvtfdgi (opnd, &flg, opc) & LMASK;
        WRITE_L (r);
        CC_IIZZ_L (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFD:
        r = op_movfd (op0);
        WRITE_Q (r, 0);
        CC_IIZZ_FP (r);
        break;

    case CVTDF:
        r = op_cvtdf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTFG:
        r = op_cvtfg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTGF:
        r = op_cvtgf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case ADDF2: case ADDF3:
        r = op_addf (opnd, FALSE);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case ADDD2: case ADDD3:
        r = op_addd (opnd, &rh, FALSE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case ADDG2: case ADDG3:
        r = op_addg (opnd, &rh, FALSE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case SUBF2: case SUBF3:
        r = op_addf (opnd, TRUE);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case SUBD2: case SUBD3:
        r = op_addd (opnd, &rh, TRUE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case SUBG2: case SUBG3:
        r = op_addg (opnd, &rh, TRUE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case MULF2: case MULF3:
        r = op_mulf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case MULD2: case MULD3:
        r = op_muld (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case MULG2: case MULG3:
        r = op_mulg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case DIVF2: case DIVF3:
        r = op_divf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case DIVD2: case DIVD3:
        r = op_divd (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case DIVG2: case DIVG3:
        r = op_divg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case ACBF:
        r = op_addf (opnd + 1, FALSE);                  /* add + index */
        temp = op_cmpfd (r, 0, op0, 0);                 /* result : limit */
        WRITE_L (r);                                    /* write result */
        CC_IIZP_FP (r);                                 /* set cc's */
        if ((temp & CC_Z) || ((op1 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

    case ACBD:
        r = op_addd (opnd + 2, &rh, FALSE);
        temp = op_cmpfd (r, rh, op0, op1);
        WRITE_Q (r, rh);
        CC_IIZP_FP (r);
        if ((temp & CC_Z) || ((op2 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

    case ACBG:
        r = op_addg (opnd + 2, &rh, FALSE);
        temp = op_cmpg (r, rh, op0, op1);
        WRITE_Q (r, rh);
        CC_IIZP_FP (r);
        if ((temp & CC_Z) || ((op2 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

/* EMODF

        op0     =       multiplier
        op1     =       extension
        op2     =       multiplicand
        op3:op4 =       integer destination (int.wl)
        op5:op6 =       floating destination (flt.wl)
*/

    case EMODF:
        r = op_emodf (opnd, &temp, &flg);
        if (op5 < 0)
            Read (op6, L_LONG, WA);
        if (op3 >= 0)
            R[op3] = temp;
        else Write (op4, temp, L_LONG, WA);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

/* EMODD, EMODG

        op0:op1 =       multiplier
        op2     =       extension
        op3:op4 =       multiplicand
        op5:op6 =       integer destination (int.wl)
        op7:op8 =       floating destination (flt.wq)
*/

    case EMODD:
        r = op_emodd (opnd, &rh, &temp, &flg);
        if (op7 < 0) {
            Read (op8, L_BYTE, WA);
            Read ((op8 + 7) & LMASK, L_BYTE, WA);
            }
        if (op5 >= 0)
            R[op5] = temp;
        else Write (op6, temp, L_LONG, WA);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case EMODG:
        r = op_emodg (opnd, &rh, &temp, &flg);
        if (op7 < 0) {
            Read (op8, L_BYTE, WA);
            Read ((op8 + 7) & LMASK, L_BYTE, WA);
            }
        if (op5 >= 0)
            R[op5] = temp;
        else Write (op6, temp, L_LONG, WA);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

/* POLY */

    case POLYF:
        op_polyf (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

    case POLYD:
        op_polyd (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

    case POLYG:
        op_polyg (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

/* Operating system instructions */

    case CHMK: case CHME: case CHMS: case CHMU:
        cc = op_chm (opnd, cc, opc);                    /* CHMx */
        GET_CUR;                                        /* update cur mode */
        SET_IRQL;                                       /* update intreq */
        break;

    case REI:
        cc = op_rei (acc);                              /* REI */
        GET_CUR;                                        /* update cur mode */
        SET_IRQL;                                       /* update intreq */
        break;

    case LDPCTX:
        op_ldpctx (acc);
        break;

    case SVPCTX:
        op_svpctx (acc);
        break;

    case PROBER: case PROBEW:
        cc = (cc & CC_C) | op_probe (opnd, opc & 1);
        break;

    case MTPR:
        mxpr_cc_vc = cc & CC_C;                         /* std: V=0, C unchgd */
        cc = op_mtpr (opnd);
        cc = cc | (mxpr_cc_vc & (CC_V|CC_C));           /* or in V,C */
        SET_IRQL;                                       /* update intreq */
        break;

    case MFPR:
        mxpr_cc_vc = cc & CC_C;                         /* std: V=0, C unchgd */
        r = op_mfpr (opnd);
        WRITE_L (r);
        CC_IIZZ_L (r);                                  /* set NV, clr VC */
        cc = cc | (mxpr_cc_vc & (CC_V|CC_C));           /* or in V,C */
        break;

/* CIS or emulated instructions */

    case CVTPL:
    case MOVP: case CMPP3: case CMPP4: case CVTLP:
    case CVTPS: case CVTSP: case CVTTP: case CVTPT:
    case ADDP4: case ADDP6: case SUBP4: case SUBP6:
    case MULP: case DIVP: case ASHP: case CRC:
    case MOVTC: case MOVTUC: case MATCHC: case EDITPC:
        cc = op_cis (opnd, cc, opc, acc);
        break;

/* Octaword or reserved instructions */

    case PUSHAO: case MOVAO: case CLRO: case MOVO:
#if defined(VAX_610)
        cc = cpu_emulate_exception (opnd, cc, opc, acc);
        break;
#endif
    case TSTH: case MOVH: case MNEGH: case CMPH:
    case CVTBH: case CVTWH: case CVTLH:
    case CVTHB: case CVTHW: case CVTHL: case CVTRHL:
    case CVTFH: case CVTDH: case CVTGH:
    case CVTHF: case CVTHD: case CVTHG:
    case ADDH2: case ADDH3: case SUBH2: case SUBH3:
    case MULH2: case MULH3: case DIVH2: case DIVH3:
    case ACBH: case POLYH: case EMODH:
        cc = op_octa (opnd, cc, opc, acc, spec, va, 
                      (hst_lnt ? &hst[hst_p ? hst_p-1 : hst_lnt -1] : NULL) );
        if (cc & LSIGN) {                               /* ACBH branch? */
            BRANCHW (brdisp);
            cc = cc & CC_MASK;                          /* mask off flag */
            }
        break;

    default:
        RSVD_INST_FAULT(opc);
        break;
        }                                               /* end case op */
    }                                                   /* end for */
}                                                       /* end sim_instr */

/* Prefetch buffer routine

   Prefetch buffer state

        ibufl, ibufh    =       the prefetch buffer
        ibcnt           =       number of bytes available (0, 4, 8)
        ppc             =       physical PC

   The get_istr routines fetches the indicated number of bytes from
   the prefetch buffer.  Although it is complicated, it is faster
   than calling Read on every byte of the instruction stream.

   If the prefetch buffer has enough bytes, the required bytes are
   extracted from the prefetch buffer and returned. If it does not
   have enough bytes, enough prefetch words are fetched until there
   are.  A longword is only prefetched if data is needed from it,
   so any translation errors are real.
*/

static SIM_INLINE int32 get_istr (int32 lnt, int32 acc)
{
int32 bo = PC & 3;
int32 sc, val, t;

while ((bo + lnt) > ibcnt) {                            /* until enuf bytes */
    if ((ppc < 0) || (VA_GETOFF (ppc) == 0)) {          /* PPC inv, xpg? */
        ppc = Test ((PC + ibcnt) & ~03, RD, &t);        /* xlate PC */
        if (ppc < 0)
            Read ((PC + ibcnt) & ~03, L_LONG, RA);
        }
    if (ibcnt == 0)                                     /* fill low */
        ibufl = ReadLP (ppc);
    else ibufh = ReadLP (ppc);                          /* or high */
    ppc = ppc + 4;                                      /* incr phys PC */
    ibcnt = ibcnt + 4;                                  /* incr ibuf cnt */
    }
PC = PC + lnt;                                          /* incr PC */
if (lnt == L_BYTE)                                      /* byte? */
    val = (ibufl >> (bo << 3)) & BMASK;
else if (lnt == L_WORD) {                               /* word? */
    if (bo == 3)
        val = ((ibufl >> 24) & 0xFF) | ((ibufh & 0xFF) << 8);
    else val = (ibufl >> (bo << 3)) & WMASK;
    }
else if (bo) {                                          /* unaligned lw? */
    sc = bo << 3;
    val =  (((ibufl >> sc) & align[bo]) | (((uint32) ibufh) << (32 - sc)));
    }
else val = ibufl;                                       /* aligned lw */
if ((bo + lnt) >= 4) {                                  /* retire ibufl? */
    ibufl = ibufh;
    ibcnt = ibcnt - 4;
    }
return val;
}

/* Read octaword specifier */

int32 ReadOcta (int32 va, int32 *opnd, int32 j, int32 acc)
{
opnd[j++] = Read (va, L_LONG, acc);
opnd[j++] = Read (va + 4, L_LONG, acc);
opnd[j++] = Read (va + 8, L_LONG, acc);  
opnd[j++] = Read (va + 12, L_LONG, acc);  
return j;
}


/* CIS instructions - invoke emulator interface

        opnd[0:5] =     six operands to be pushed (if PSL<fpd> = 0)
        cc      =       condition codes
        opc     =       opcode

   If FPD is set, push old PC and PSL on stack, vector thru SCB.
   If FPD is clear, push opcode, old PC, operands, new PC, and PSL
        on stack, vector thru SCB.
   In both cases, the exception occurs in the current mode.
*/

int32 cpu_emulate_exception (int32 *opnd, int32 cc, int32 opc, int32 acc)
{
int32 vec;

if (PSL & PSL_FPD) {                                    /* FPD set? */
    Read (SP - 1, L_BYTE, WA);                          /* wchk stack */
    Write (SP - 8, fault_PC, L_LONG, WA);               /* push old PC */       
    Write (SP - 4, PSL | cc, L_LONG, WA);               /* push PSL */
    SP = SP - 8;                                        /* decr stk ptr */
    vec = ReadLP ((SCBB + SCB_EMULFPD) & PAMASK);
    sim_debug (LOG_CPU_FAULT_EMUL, &cpu_dev, "FPD OP=%s, fault_PC=%08x, PC=%08x, PSL=%08x, SP=%08x, nPC=%08x ",
                 opcode[opc], fault_PC, PC, PSL, SP, vec);
    sim_debug_bits(LOG_CPU_FAULT_EMUL, &cpu_dev, cpu_psl_bits, PSL, PSL, 1);
    }
else {
    if (opc == CVTPL)                                   /* CVTPL? .wl */
        opnd[2] = (opnd[2] >= 0)? ~opnd[2]: opnd[3];
    Read (SP - 1, L_BYTE, WA);                          /* wchk stack */
    Write (SP - 48, opc, L_LONG, WA);                   /* push opcode */
    Write (SP - 44, fault_PC, L_LONG, WA);              /* push old PC */
    Write (SP - 40, opnd[0], L_LONG, WA);               /* push operands */
    Write (SP - 36, opnd[1], L_LONG, WA);
    Write (SP - 32, opnd[2], L_LONG, WA);
    Write (SP - 28, opnd[3], L_LONG, WA);
    Write (SP - 24, opnd[4], L_LONG, WA);
    Write (SP - 20, opnd[5], L_LONG, WA);
    Write (SP - 8, PC, L_LONG, WA);                     /* push cur PC */
    Write (SP - 4, PSL | cc, L_LONG, WA);               /* push PSL */
    SP = SP - 48;                                       /* decr stk ptr */
    vec = ReadLP ((SCBB + SCB_EMULATE) & PAMASK);
    sim_debug (LOG_CPU_FAULT_EMUL, &cpu_dev, "OP=%s, fault_PC=%08x, PC=%08x, PSL=%08x, SP=%08x, nPC=%08x ",
                 opcode[opc], fault_PC, PC, PSL, SP, vec);
    sim_debug_bits(LOG_CPU_FAULT_EMUL, &cpu_dev, cpu_psl_bits, PSL, PSL, 1);
    }
PSL = PSL & ~(PSL_TP | PSL_FPD | PSW_DV | PSW_FU | PSW_IV | PSW_T);
JUMP (vec & ~03);                                       /* set new PC */
return 0;                                               /* set new cc's */
}


/* Idle before the next instruction */

void cpu_idle (void)
{
sim_idle (TMR_CLK, TRUE);
}

/*
 * This sequence of instructions is a mix that mimics
 * a resonable instruction set that is a close estimate
 * to the calibrated result without a direct "loop to 
 * self" instruction that would halt simulation.
 */

static const char *vax_clock_precalibrate_commands[] = {
    "-m 100 INCL  120",
    "-m 103 INCL  124",
    "-m 106 MULL3 120,124,128",
    "-m 10D BRW   100",
    "PC 100",
    NULL};

/* Reset */

t_stat cpu_reset (DEVICE *dptr)
{
hlt_pin = 0;
mem_err = 0;
crd_err = 0;
PSL = PSL_IS | PSL_IPL1F;
SISR = 0;
ASTLVL = 4;
mapen = 0;
FLUSH_ISTR;                             /* init I-stream */
if (M == NULL) {                        /* first time init? */
    vax_init();
    sim_brk_types = sim_brk_dflt = SWMASK ('E');
    sim_vm_is_subroutine_call = cpu_is_pc_a_subroutine_call;
    sim_clock_precalibrate_commands = vax_clock_precalibrate_commands;
    sim_vm_initial_ips = SIM_INITIAL_IPS;
    pcq_r = find_reg ("PCQ", NULL, dptr);
    if (pcq_r == NULL)
        return SCPE_IERR;
    pcq_r->qptr = 0;
    M = (uint32 *) calloc (((uint32) MEMSIZE) >> 2, sizeof (uint32));
    if (M == NULL)
        return SCPE_MEM;
    auto_config(NULL, 0);               /* do an initial auto configure */
    }
return build_dib_tab ();
}

static const char *cpu_next_caveats =
"The NEXT command in this VAX architecture simulator currently will\n"
"enable stepping across subroutine calls which are initiated by the\n"
"BSBB, BSBW, JSB, CALLG, CALLS, CHMK, CHME, CHMS, and CHMU instructions.\n"
"This stepping works by dynamically establishing breakpoints at the\n"
"memory address immediately following the instruction which initiated\n"
"the subroutine call.  These dynamic breakpoints are automatically\n"
"removed once the simulator returns to the sim> prompt for any reason.\n"
"If the called routine returns somewhere other than one of these\n"
"locations due to a trap, stack unwind or any other reason, instruction\n"
"execution will continue until some other reason causes execution to stop.\n";

t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs)
{
#define MAX_SUB_RETURN_SKIP 9
static t_addr returns[MAX_SUB_RETURN_SKIP+1] = {0};
static t_bool caveats_displayed = FALSE;
int i;
int32 saved_sim_switches = sim_switches;

if (!caveats_displayed) {
    caveats_displayed = TRUE;
    sim_printf ("%s", cpu_next_caveats);
    }
sim_switches |= SWMASK('V');
if (SCPE_OK != get_aval (PC, &cpu_dev, &cpu_unit)) {/* get data */
    sim_switches = saved_sim_switches;
    return FALSE;
    }
sim_switches = saved_sim_switches;
switch (sim_eval[0])
    {
    case BSBB:  case BSBW:  case JSB:
    case CALLG: case CALLS:
    case CHMK:  case CHME:  case CHMS:  case CHMU:
        returns[0] = PC + (1 - fprint_sym (stdnul, PC, sim_eval, &cpu_unit, SWMASK ('M')));
        for (i=1; i<MAX_SUB_RETURN_SKIP; i++)
            returns[i] = returns[i-1] + 1;      /* Possible skip return */
        returns[i] = 0;                         /* Make sure the address list ends with a zero */
        *ret_addrs = returns;
        return TRUE;
    default:
        return FALSE;
    }
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;

if (vptr == NULL) 
    return SCPE_ARG;
if (sw & SWMASK ('V')) {
    int32 acc = cpu_get_vsw (sw);
    addr = Test (addr, acc, &st);
    }
else addr = addr & PAMASK;
if (ADDR_IS_MEM (addr) || ADDR_IS_CDG (addr) ||
    ADDR_IS_ROM (addr) || ADDR_IS_NVR (addr)) {
    *vptr = (uint32) ReadB (addr);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;

if (sw & SWMASK ('V')) {
    int32 acc = cpu_get_vsw (sw);
    addr = Test (addr, acc, &st);
    }
else addr = addr & PAMASK;
if (ADDR_IS_MEM (addr) || ADDR_IS_CDG (addr) ||
    ADDR_IS_NVR (addr)) {
    WriteB (addr, (int32) val);
    return SCPE_OK;
    }
if (ADDR_IS_ROM (addr)) {
    rom_wr_B (addr, (int32) val);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i, clim, uval = (uint32)val;
uint32 *nM = NULL;

if ((val <= 0) || (val > MAXMEMSIZE_X))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i = i + 4)
    mc = mc | M[i >> 2];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
nM = (uint32 *) calloc (uval >> 2, sizeof (uint32));
if (nM == NULL)
    return SCPE_MEM;
clim = (uint32)((uval < MEMSIZE)? uval: MEMSIZE);
for (i = 0; i < clim; i = i + 4)
    nM[i >> 2] = M[i >> 2];
free (M);
M = nM;
MEMSIZE = uval; 
reset_all (0);
return SCPE_OK;
}

/* Virtual address translation */

t_stat cpu_show_virt (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
t_stat r;
const char *cptr = (const char *) desc;
uint32 va, pa;
int32 st;
static const char *mm_str[] = {
    "Access control violation",
    "Length violation",
    "Process PTE access control violation",
    "Process PTE length violation",
    "Translation not valid",
    "Internal error",
    "Process PTE translation not valid"
    };

if (cptr) {
    va = (uint32) get_uint (cptr, 16, 0xFFFFFFFF, &r);
    if (r == SCPE_OK) {
        int32 acc = cpu_get_vsw (sim_switches);
        pa = Test (va, acc, &st);
        if (st == PR_OK)
            fprintf (of, "Virtual %-X = physical %-X\n", va, pa);
        else fprintf (of, "Virtual %-X: %s\n", va, mm_str[st]);
        return SCPE_OK;
        }
    }
fprintf (of, "Invalid argument\n");
return SCPE_OK;
}

/* Get access mode for examine, deposit, show virtual */

int32 cpu_get_vsw (int32 sw)
{
int32 md;

set_map_reg ();                                         /* update dyn reg */
if (sw & SWMASK ('K'))
    md = KERN;
else if (sw & SWMASK ('E')) 
    md = EXEC;
else if (sw & SWMASK ('S'))
    md = SUPV;
else if (sw & SWMASK ('U'))
    md = USER;
else md = PSL_GETCUR (PSL);
return ACC_MASK (md);
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
char gbuf[CBUFSIZE];
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].iPC = 0;
    hst_p = 0;
    if (hst_log) {
        sim_set_fsize (hst_log, (t_addr)0);
        hst_log_p = 0;
        cpu_show_hist_records (hst_log, TRUE, 0, 0);
        }
    return SCPE_OK;
    }
cptr = get_glyph (cptr, gbuf, ':');
lnt = (int32) get_uint (gbuf, 10, HIST_MAX, &r);
if (r != SCPE_OK)
    return sim_messagef (SCPE_ARG, "Invalid Numeric Value: %s\n", gbuf);
if (lnt && (lnt < HIST_MIN))
    return sim_messagef (SCPE_ARG, "%d is less than the minumum history value of %d\n", lnt, HIST_MIN);
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    if (hst_log) {
        fclose (hst_log);
        hst_log = NULL;
        }
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
            return SCPE_MEM;
    hst_lnt = lnt;
    hst_switches = sim_switches;
    if (cptr && *cptr) {
        hst_log = sim_fopen (cptr, "w");
        if (hst_log)
            cpu_show_hist_records (hst_log, TRUE, 0, 0);
        else {
            free (hst);
            hst_lnt = 0;
            hst = NULL;
            return sim_messagef(SCPE_OPENERR, "Unable to open file '%s': %s\n", cptr, strerror (errno));
            }            
        }
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 di, lnt;
const char *cptr = (const char *) desc;
t_stat r;

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0))
        return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0)
    di = di + hst_lnt;
return cpu_show_hist_records (st, TRUE, di, lnt);
}

t_stat cpu_show_hist_records (FILE *st, t_bool do_header, int32 start, int32 count)
{
int32 i, k, numspec;
InstHistory *h;

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (do_header) {
    if (hst_switches & SWMASK('T'))
        fprintf (st," TIME       ");
    fprintf (st, "PC       PSL       IR\n\n");
    }
for (k = 0; k < count; k++) {                           /* print specified */
    h = &hst[(start++) % hst_lnt];                      /* entry pointer */
    if (h->iPC == 0)                                    /* filled in? */
        continue;
    if (hst_switches & SWMASK('T'))                     /* sim_time */
        fprintf(st, "%10.0f  ", h->time);
    fprintf(st, "%08X %08X| ", h->iPC, h->PSL);         /* PC, PSL */
    numspec = DR_GETNSP (drom[h->opc][0]);              /* #specifiers */
    if (opcode[h->opc] == NULL)                         /* undefined? */
        fprintf (st, "%03X (undefined)", h->opc);
    else if (h->PSL & PSL_FPD)                          /* FPD set? */
        fprintf (st, "%s FPD set", opcode[h->opc]);
    else {                                              /* normal */
        for (i = 0; i < INST_SIZE; i++)
            sim_eval[i] = h->inst[i];
        if ((fprint_sym (st, h->iPC, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "%03X (undefined)", h->opc);
        if ((numspec > 1) ||
            ((numspec == 1) && (drom[h->opc][1] < BB))) {
            if (cpu_show_opnd (st, h, 0)) {             /* operands; more? */
                if (cpu_show_opnd (st, h, 1)) {         /* 2nd line; more? */
                    cpu_show_opnd (st, h, 2);           /* octa, 3rd/4th */
                    cpu_show_opnd (st, h, 3);
                    }
                }
            }
        }                                               /* end else */
    fputc ('\n', st);                                   /* end line */
    }                                                   /* end for */
fflush (st);
return SCPE_OK;
}

t_bool cpu_show_opnd (FILE *st, InstHistory *h, int32 line)
{

int32 numspec, i, j, disp;
t_bool more;

numspec = drom[h->opc][0] & DR_NSPMASK;                 /* #specifiers */
fputs ("\n                  ", st);                     /* space */
if (hst_switches & SWMASK('T'))
    fputs ("            ", st);
for (i = 1, j = 0, more = FALSE; i <= numspec; i++) {   /* loop thru specs */
    disp = drom[h->opc][i];                             /* specifier type */
    if (disp == RG)                                     /* fix specials */
        disp = RQ;
    if (disp >= BB)                                     /* ignore branches */
        break;
    switch (disp & (DR_LNMASK|DR_ACMASK)) {

    case RB: case RW: case RL:                          /* read */
    case AB: case AW: case AL: case AQ: case AO:        /* address */
    case MB: case MW: case ML:                          /* modify */
        if (line == 0)
            fprintf (st, " %08X", h->opnd[j]);
        else fputs ("         ", st);
        j = j + 1;
        break;
    case RQ: case MQ:                                   /* read, modify quad */
        if (line <= 1)
            fprintf (st, " %08X", h->opnd[j + line]);
        else fputs ("         ", st);
        if (line == 0)
            more = TRUE;
        j = j + 2;
        break;
    case RO: case MO:                                   /* read, modify octa */
        fprintf (st, " %08X", h->opnd[j + line]);
        more = TRUE;
        j = j + 4;
        break;
    case WB: case WW: case WL: case WQ: case WO:        /* write */
        if (line == 0)
            fprintf (st, " %08X", h->opnd[j + 1]);
        else fputs ("         ", st);
        j = j + 2;
        break;
        }                                       /* end case */
    }                                           /* end for */
if ((line == 0) && (DR_GETRES(drom[h->opc][0]))) {
    fprintf (st, " ->");
    switch (DR_GETRES(drom[h->opc][0]) << DR_V_RESMASK) {
        case RB_O:
            fprintf (st, " %08X %08X %08X %08X", h->res[0], h->res[1], h->res[2], h->res[3]);
            break;
        case RB_Q:
            fprintf (st, " %08X %08X", h->res[0], h->res[1]);
            break;
        case RB_B:
        case RB_W:
        case RB_L:
            fprintf (st, " %08X", h->res[0]);
            break;
        case RB_R5:
        case RB_R3:
        case RB_R1:
        case RB_R0:
            if (1) {
                static const int rcnts[] = {1, 2, 4, 6};
                int i;

                for (i = 0; i < rcnts[DR_GETRES(drom[h->opc][0]) - DR_GETRES(RB_R0)]; i++)
                    fprintf (st, " R%d:%08X", i, h->res[i]);
                }
            break;
        case RB_SP:
            fprintf (st, " SP: %08X", h->res[0]);
            break;
        default:
            break;
        }
    }
return more;
}

struct os_idle {
    const char        *name;
    uint32      mask;
    };

static struct os_idle os_tab[] = {
    { "VMS",            VAX_IDLE_VMS },
    { "INFOSERVER",     VAX_IDLE_INFOSERVER },
    { "ULTRIX",         VAX_IDLE_ULT },
    { "ULTRIXOLD",      VAX_IDLE_ULTOLD },
    { "ULTRIX-1.X",     VAX_IDLE_ULT1X },
    { "3BSD",           VAX_IDLE_ULT1X },
    { "4.0BSD",         VAX_IDLE_ULT1X },
    { "4.1BSD",         VAX_IDLE_ULT1X },
    { "4.2BSD",         VAX_IDLE_ULT1X },
    { "QUASIJARUS",     VAX_IDLE_QUAD },
    { "4.3BSD",         VAX_IDLE_QUAD },
    { "4.4BSD-Reno",    VAX_IDLE_QUAD },
    { "NETBSD",         VAX_IDLE_BSDNEW },
    { "NETBSDOLD",      VAX_IDLE_ULTOLD },
    { "OPENBSD",        VAX_IDLE_BSDNEW },
    { "OPENBSDOLD",     VAX_IDLE_QUAD },
    { "32V",            VAX_IDLE_VMS },
    { "ELN",            VAX_IDLE_ELN },
    { "MDM",            VAX_IDLE_ELN },
    { NULL, 0 }
    };

/* Set and show idle */

t_stat cpu_set_idle (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i;
char gbuf[CBUFSIZE];

if (cptr != NULL) {
    cptr = get_glyph (cptr, gbuf, ':');
    for (i = 0; os_tab[i].name != NULL; i++) {
        if (strcmp (os_tab[i].name, gbuf) == 0) {
            cpu_idle_type = i + 1;
            cpu_idle_mask = os_tab[i].mask;
            return sim_set_idle (uptr, val, cptr, desc);
            }
        }
    return SCPE_ARG;
    }
return sim_set_idle (uptr, val, cptr, desc);
}

t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (sim_idle_enab && (cpu_idle_type != 0))
    fprintf (st, "idle=%s, ", os_tab[cpu_idle_type - 1].name);
sim_show_idle (st, uptr, val, desc);
return SCPE_OK;
}
 
static struct {
    int32 mask;
    const char *match;
    const char *desc;
    } inst_groups[] = {
        {0,          "",            ""},                            /* Reserved Opcode */
        {VAX_BASE,   "BASE",        "Base Group"},                  /* Base Instruction Group       */
        {VAX_GFLOAT, "G-FLOAT",     "G-Float"},                     /*   Base subgroup G-Float      */
        {VAX_DFLOAT, "D-FLOAT",     "D-Float"},                     /*   Base subgroup D-Float      */
        {VAX_PACKED, "PACKED",      "Packed-Decimal-String-Group"}, /* packed-decimal-string group  */
        {VAX_EXTAC,  "EXTENDED",    "Extended-Accuracy-Group"},     /* extended-accuracy group      */
        {VAX_EMONL,  "EMULATED",    "Emulated-Only-Group"},         /* emulated only instructions   */
//      {VAX_VECTR,  "VECTOR",      "Vector-Processing-Group"},     /* vector-processing group      */
        {0,          NULL,          NULL}
    };

t_stat cpu_set_instruction_set (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (!cptr || !*cptr)
    return SCPE_ARG;
while (1) {
    int i;
    t_bool invert = FALSE;
    char gbuf[CBUFSIZE];

    cptr = get_glyph (cptr, gbuf, ';');
    if (!gbuf[0])
        break;
    if (0 == strncmp (gbuf, "NO", 2)) {
        invert = TRUE;
        memmove (gbuf, gbuf + 2, 1 + strlen (gbuf + 2));
        }
    for (i=0; inst_groups[i].match != NULL; i++)
        if (MATCH_CMD (gbuf, inst_groups[i].match) == 0)
            break;
    if (inst_groups[i].match == NULL)
        return sim_messagef (SCPE_ARG, "unknown instruction set group: %s\n", gbuf);
    if (invert)
        cpu_instruction_set &= ~inst_groups[i].mask;
    else
        cpu_instruction_set |= inst_groups[i].mask;
    }
return SCPE_OK;
}

/* Used when sorting a list of opcode names */
static int _opc_name_compare (const void *pa, const void *pb)
{
const char **a = (const char **)pa;
const char **b = (const char **)pb;

return strcmp (*a, *b);
}

t_stat cpu_show_instruction_group (FILE *st, int32 groupmask)
{
int opc;
int group;
int matches;
char const *opcd_tmp[NUM_INST];

for (opc=matches=0; opc<NUM_INST; opc++) {
    t_bool match = FALSE;

    for (group=0; (!match) && (group<=IG_MAX_GRP); group++) {
        if ((1 << group) & groupmask)
            match = (DR_GETIGRP(drom[opc][0]) == group);
        }
    if (match)
        opcd_tmp[matches++] = opcode[opc];
    }
qsort ((void *)opcd_tmp, matches, sizeof (opcd_tmp[0]), _opc_name_compare);
for (opc=0; opc<matches; opc++)
    fprintf (st, "%s\t%s", (0 == opc%8) ? "\n" : "", opcd_tmp[opc]);
fprintf (st, "\n");
return SCPE_OK;
}

t_stat cpu_show_instruction_set (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int i;

fprintf (st, "Implementing: ");
if ((cpu_instruction_set & FULL_INSTRUCTION_SET) == FULL_INSTRUCTION_SET) {
    fprintf (st, "All standard VAX instructions");
#if defined(CMPM_VAX)
    fprintf (st, " and Compatibility mode");
#endif
    if (sim_switches & SWMASK ('V'))
        cpu_show_instruction_group (st, cpu_instruction_set);
//  if (cpu_instruction_set & ~VAX_VECTR)
//      fprintf (st, ",\n\tEmulating: Vector-Group");
    }
else {
    if ((cpu_instruction_set & VAX_FULL_BASE) == VAX_FULL_BASE) {
        fprintf (st, "Base Instruction Group");
        if (sim_switches & SWMASK ('V'))
            cpu_show_instruction_group (st, VAX_FULL_BASE);
        }
    else {
        if ((cpu_instruction_set & VAX_BASE) == VAX_BASE) {
            fprintf (st, "Base Instruction Group");
            if (!(cpu_instruction_set & VAX_GFLOAT)) {
                fprintf (st, " without G-Float");
                if (!(cpu_instruction_set & VAX_DFLOAT))
                    fprintf (st, " and D-Float");
                }
            else {
                if (!(cpu_instruction_set & VAX_DFLOAT))
                    fprintf (st, " without D-Float");
                }
            }
        else {
            if (!(cpu_instruction_set & VAX_DFLOAT))
                fprintf (st, " without D-Float");
            }
        if (sim_switches & SWMASK ('V'))
            cpu_show_instruction_group (st, cpu_instruction_set);
        }
    for (i=4; inst_groups[i].match; i++) {
        if (cpu_instruction_set & inst_groups[i].mask) {
            fprintf (st, " %s", inst_groups[i].desc);
            if (sim_switches & SWMASK ('V'))
                cpu_show_instruction_group (st, inst_groups[i].mask);
            }
        }
    fprintf (st, "\n%sEmulating:", (sim_switches & SWMASK ('V')) ? "" : "\t");
    for (i=1; inst_groups[i].match; i++) {
        if (!(cpu_instruction_set & inst_groups[i].mask)) {
            fprintf (st, " %s", inst_groups[i].desc);
            if (sim_switches & SWMASK ('V'))
                cpu_show_instruction_group (st, inst_groups[i].mask);
            }
        }
    }
return SCPE_OK;
}

t_stat cpu_load_bootcode (const char *filename, const unsigned char *builtin_code, size_t size, t_bool rom, t_addr offset)
{
char args[CBUFSIZE];
t_stat r;
int32 saved_sim_switches = sim_switches;

sim_messagef (SCPE_OK, "Loading boot code from %s%s\n", builtin_code ? "internal " : "", filename);
if (builtin_code)
    sim_set_memory_load_file (builtin_code, size);
if (rom)
    sprintf (args, "-R %s", filename);
else
    sprintf (args, "-O %s %X", filename, (int)offset);
r = load_cmd (0, args);
sim_set_memory_load_file (NULL, 0);
sim_switches = saved_sim_switches;
return r;
}

t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "The ");cpu_print_model (st);fprintf (st, " CPU help\n\n");
fprintf (st, "CPU options include the size of main memory.\n\n");
if (dptr->modifiers) {
    MTAB *mptr;

    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++)
        if (mptr->valid == &cpu_set_size)
            fprintf (st, "   sim> SET CPU %4s                    set memory size = %sB\n", mptr->mstring, mptr->mstring);
    fprintf (st, "\n");
    }
cpu_model_help (st, dptr, uptr, flag, cptr);
fprintf (st, "CPU options include the treatment of the HALT instruction.\n\n");
fprintf (st, "   sim> SET CPU SIMHALT                 kernel HALT returns to simulator\n");
fprintf (st, "   sim> SET CPU CONHALT                 kernel HALT returns to boot ROM console\n\n");
fprintf (st, "The CPU also implements a command to display a virtual to physical address\n");
fprintf (st, "translation:\n\n");
fprintf (st, "   sim> SHOW {-kesu} CPU VIRTUAL=n      show translation for address n\n");
fprintf (st, "                                        in kernel/exec/supervisor/user mode\n\n");
fprintf (st, "Memory can be loaded with a binary byte stream using the LOAD command.  The\n");
fprintf (st, "LOAD command recognizes three switches:\n\n");
fprintf (st, "      -o      origin argument follows file name\n");
fprintf (st, "      -r      load the boot ROM\n");
fprintf (st, "      -n      load the non-volatile RAM\n\n");
fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
fprintf (st, "      -b      examine/deposit bytes\n");
fprintf (st, "      -w      examine/deposit words\n");
fprintf (st, "      -l      examine/deposit longwords\n");
fprintf (st, "      -d      data radix is decimal\n");
fprintf (st, "      -o      data radix is octal\n");
fprintf (st, "      -h      data radix is hexadecimal\n");
fprintf (st, "      -m      examine (only) VAX instructions\n");
fprintf (st, "      -p      examine/deposit PDP-11 (compatibility mode) instructions\n");
fprintf (st, "      -r      examine (only) RADIX50 encoded data\n");
fprintf (st, "      -v      interpret address as virtual, current mode\n");
fprintf (st, "      -k      interpret address as virtual, kernel mode\n");
fprintf (st, "      -e      interpret address as virtual, executive mode\n");
fprintf (st, "      -s      interpret address as virtual, supervisor mode\n");
fprintf (st, "      -u      interpret address as virtual, user mode\n\n");
fprintf (st, "The CPU attempts to detect when the simulator is idle.  When idle, the\n");
fprintf (st, "simulator does not use any resources on the host system.  Idle detection is\n");
fprintf (st, "controlled by the SET IDLE and SET NOIDLE commands:\n\n");
fprintf (st, "   sim> SET CPU IDLE{=VMS|ULTRIX|ULTRIXOLD|ULTRIX-1.X|\n");
fprintf (st, "                      3BSD|4.0BSD|4.1BSD|4.2BSD|QUASIJARUS|\n");
fprintf (st, "                      NETBSD|NETBSDOLD|OPENBSD|OPENBSDOLD|\n");
fprintf (st, "                      32V|ELN|INFOSERVER}{:n}\n");
fprintf (st, "                                        enable idle detection\n");
fprintf (st, "   sim> SET CPU NOIDLE                  disable idle detection\n\n");
fprintf (st, "Idle detection is disabled by default.  If idle detection is enabled with\n");
fprintf (st, "an incorrect operating system setting, simulator performance or correct\n");
fprintf (st, "functionality could be impacted.  The default operating system setting is\n");
fprintf (st, "VMS.  The value 'n', if present in the \"SET CPU IDLE={OS}:n\" command,\n");
fprintf (st, "indicats the number of seconds which the simulator must run before idling\n");
fprintf (st, "starts.\n\n");
fprintf (st, "The CPU can maintain a history of the most recently executed instructions.\n");
fprintf (st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n");
fprintf (st, "   sim> SET CPU HISTORY                 clear history buffer\n");
fprintf (st, "   sim> SET CPU HISTORY=0               disable history\n");
fprintf (st, "   sim> SET CPU {-T} HISTORY=n{:file}   enable history, length = n\n");
fprintf (st, "   sim> SHOW CPU HISTORY                print CPU history\n");
fprintf (st, "   sim> SHOW CPU HISTORY=n              print first n entries of CPU history\n\n");
fprintf (st, "The -T switch causes simulator time to be recorded (and displayed)\n");
fprintf (st, "with each history entry.\n");
fprintf (st, "When writing history to a file (SET CPU HISTORY=n:file), 'n' specifies\n");
fprintf (st, "the buffer flush frequency.  Warning: prodigious amounts of disk space\n");
fprintf (st, "may be comsumed.  The maximum length for the history is %d entries.\n\n", HIST_MAX);
fprintf (st, "Different VAX systems implemented different VAX architecture instructions\n");
fprintf (st, "in hardware with other instructions possibly emulated by software in the\n");
fprintf (st, "system.  The instructions that a particular simulator implements can be\n");
fprintf (st, "displayed with:\n\n");
fprintf (st, "   sim> SHOW CPU INSTRUCTIONS     display the instructoin groups that are\n");
fprintf (st, "                                  implemented and emulated\n");
fprintf (st, "   sim> SHOW CPU -V INSTRUCTIONS  disable the list of instructions implemented\n");
fprintf (st, "                                  and emulated\n\n");
return SCPE_OK;
}
