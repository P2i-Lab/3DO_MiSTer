/*
  P2i Lab 3DO bench 3.0, groups ARM TEMPS and ARM CRC. The code under test is generated
  at run time as machine words, so that each measurement is exactly the intended
  instructions, whatever the compiler does.

  ARM TEMPS: time of loops around one instruction or a short sequence. The loop is
  subs r0,r0,#1 / bne, so a loop's time minus the time of the empty loop is the cost
  of its body. The numbers appear in the order of the cases[] table.

  ARM CRC: random instruction sequences run from known registers and flags. Each line
  is the CRC32 of the final registers and flags of one family of instructions, so two
  machines can be compared without knowing the expected results. Only r0-r8 are used
  and memory instructions stay inside a scratch area owned by this task.
*/

#include "bench.h"

#include "controlpad.h"
#include "event.h"
#include "item.h"
#include "kernel.h"
#include "task.h"

#include "mem.h"
#include "string.h"

#define JIT_WORDS   4096
#define DATA_BYTES  4096
#define SCR_BYTES   1024
#define SCR_MID     512

typedef u32 (*JitFn)(u32, u32, u32, u32);
typedef void (*JitState)(u32 *);

static u32 *jit_d;
static u32 *jit_v;
static u32 *data_d;
static u32 *data_v;
static u8  *scratch;
static u8  *scratch0;
static Stat st;

#define PUSH_ALL  0xE92D4FF0u  /* stmfd sp!,{r4-r11,lr} */
#define POP_ALL   0xE8BD8FF0u  /* ldmfd sp!,{r4-r11,pc} */
#define SUBS_R0   0xE2500001u  /* subs r0,r0,#1 */
#define MOV_R5    0xE3A05005u  /* mov r5,#5 */
#define MOV_R6    0xE3A06006u  /* mov r6,#6 */
#define MOV_R7    0xE3A07003u  /* mov r7,#3 */
#define NOP4      0xE1A04004u  /* mov r4,r4 */
#define MUL_R1    0xE0040195u  /* mul r4,r5,r1 */
#define MLA_R1    0xE0246195u  /* mla r4,r5,r1,r6 */

#define P_NONE  0
#define P_DRAM  1
#define P_VRAM  2
#define P_VALUE 3

typedef struct Case
{
  u32 w0;
  u32 w1;
  u32 words;
  u32 rep;
  u32 iters;
  u32 par;
  u32 val;
} Case;

#define LOOPS 32768
#define MLOOP 16384

static const Case cases[] =
  {
    /* 0 */  { 0,           0,           0, 1, LOOPS, P_NONE, 0 },            /* empty loop */
    /* 1 */  { NOP4,        0,           1, 1, LOOPS, P_NONE, 0 },
    /* 2 */  { NOP4,        0,           1, 2, LOOPS, P_NONE, 0 },
    /* 3 */  { NOP4,        0,           1, 4, LOOPS, P_NONE, 0 },
    /* 4 */  { 0xE0854006u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* add r4,r5,r6 */
    /* 5 */  { 0xE0854186u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* add r4,r5,r6,lsl #3 */
    /* 6 */  { 0xE0854716u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* add r4,r5,r6,lsl r7 */
    /* 7 */  { 0x00854006u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* addeq: not executed */
    /* 8 */  { 0x0A000000u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* beq: not taken */
    /* 9 */  { 0xEAFFFFFFu, 0,           1, 1, LOOPS, P_NONE, 0 },            /* b next */
    /* 10 */ { 0xEBFFFFFFu, 0,           1, 1, LOOPS, P_NONE, 0 },            /* bl next */
    /* 11 */ { 0xE28FF000u, 0xE1A00000u, 2, 1, LOOPS, P_NONE, 0 },            /* add pc,pc,#0 + skipped nop */
    /* 12 */ { 0xE10F4000u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* mrs r4,cpsr */
    /* 13 */ { 0xE128F004u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* msr cpsr_f,r4 */
    /* 14 */ { 0xE2454001u, 0,           1, 1, LOOPS, P_NONE, 0 },            /* sub r4,r5,#1 */

    /* multiplication: the time depends on the value of Rs */
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000000u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000001u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000003u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x0000000Fu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x000000FFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000100u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000FFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x0000FFFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00010000u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x000FFFFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00FFFFFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x01000000u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x7FFFFFFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0x80000000u },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0xFFFFFFFFu },
    { MUL_R1, 0, 1, 1, MLOOP, P_VALUE, 0xFFFF0000u },
    { MLA_R1, 0, 1, 1, MLOOP, P_VALUE, 0x00000001u },
    { MLA_R1, 0, 1, 1, MLOOP, P_VALUE, 0xFFFFFFFFu },

    /* memory, DRAM */
    { 0xE5914000u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* ldr r4,[r1] */
    { 0xE5914000u, 0xE5915004u, 2, 1, MLOOP, P_DRAM, 0 },   /* two ldr, next words */
    { 0xE5814000u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* str r4,[r1] */
    { 0xE5D14000u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* ldrb */
    { 0xE5C14000u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* strb */
    { 0xE8910010u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* ldmia, 1 register */
    { 0xE8910030u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 2 */
    { 0xE89100F0u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 4 */
    { 0xE89153F0u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 8 */
    { 0xE8810010u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* stmia, 1 register */
    { 0xE8810030u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 2 */
    { 0xE88100F0u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 4 */
    { 0xE88153F0u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* 8 */
    { 0xE1014095u, 0,           1, 1, MLOOP, P_DRAM, 0 },   /* swp r4,r5,[r1] */

    /* the same in VRAM */
    { 0xE5914000u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE5914000u, 0xE5915004u, 2, 1, MLOOP, P_VRAM, 0 },
    { 0xE5814000u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE5D14000u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE5C14000u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE8910010u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE8910030u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE89100F0u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE89153F0u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE8810010u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE8810030u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE88100F0u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE88153F0u, 0,           1, 1, MLOOP, P_VRAM, 0 },
    { 0xE1014095u, 0,           1, 1, MLOOP, P_VRAM, 0 },

    /* runs of sequential instructions */
    { NOP4, 0, 1, 16,   8192, P_NONE, 0 },
    { NOP4, 0, 1, 64,   2048, P_NONE, 0 },
    { NOP4, 0, 1, 256,  512,  P_NONE, 0 },
    { NOP4, 0, 1, 1024, 128,  P_NONE, 0 }
  };

#define NCASES (sizeof(cases) / sizeof(cases[0]))

static void arm_free(void)
{
  if(jit_d != NULL)    FreeMem(jit_d, JIT_WORDS * 4);
  if(jit_v != NULL)    FreeMem(jit_v, JIT_WORDS * 4);
  if(data_d != NULL)   FreeMem(data_d, DATA_BYTES);
  if(data_v != NULL)   FreeMem(data_v, DATA_BYTES);
  if(scratch != NULL)  FreeMem(scratch, SCR_BYTES);
  if(scratch0 != NULL) FreeMem(scratch0, SCR_BYTES);
  jit_d = NULL;
  jit_v = NULL;
  data_d = NULL;
  data_v = NULL;
  scratch = NULL;
  scratch0 = NULL;
}

static int arm_setup(void)
{
  jit_d    = (u32 *)AllocMem(JIT_WORDS * 4, MEMTYPE_DRAM | MEMTYPE_FILL);
  jit_v    = (u32 *)AllocMem(JIT_WORDS * 4, MEMTYPE_VRAM | MEMTYPE_FILL);
  data_d   = (u32 *)AllocMem(DATA_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL);
  data_v   = (u32 *)AllocMem(DATA_BYTES, MEMTYPE_VRAM | MEMTYPE_FILL);
  scratch  = (u8 *)AllocMem(SCR_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL);
  scratch0 = (u8 *)AllocMem(SCR_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL);
  if(jit_d == NULL || jit_v == NULL || data_d == NULL || data_v == NULL ||
     scratch == NULL || scratch0 == NULL)
    {
      arm_free();
      return -1;
    }
  return 0;
}

static void build(u32 *code, const Case *c)
{
  u32 i;
  u32 j;
  u32 n;
  u32 loop;

  n = 0;
  code[n++] = PUSH_ALL;
  code[n++] = MOV_R5;
  code[n++] = MOV_R6;
  code[n++] = MOV_R7;
  loop = n;
  for(i = 0; i < c->rep; i++)
    {
      if(c->words >= 1)
        code[n++] = c->w0;
      if(c->words >= 2)
        code[n++] = c->w1;
      j = 0;
      (void)j;
    }
  code[n++] = SUBS_R0;
  code[n] = 0x1A000000u | ((loop - n - 2) & 0x00FFFFFF);   /* bne loop */
  n++;
  code[n++] = POP_ALL;
}

static u32 run_code(u32 *code, u32 iters, u32 par)
{
  JitFn f;
  u32 t0;
  u32 i;

  f = (JitFn)code;
  stat_reset(&st);
  for(i = 0; i < 5; i++)
    {
      t0 = usec_now();
      b_sink ^= f(iters, par, 0, 0);
      stat_add(&st, usec_now() - t0);
    }
  return stat_median(&st);
}

void group_arm_time(void)
{
  u32 i;
  u32 par;
  const Case *c;
  Case v;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress(L("BOUCLES", "LOOPS  "));
  grid_begin('T', 5);
  for(i = 0; i < NCASES; i++)
    {
      c = &cases[i];
      par = 0;
      if(c->par == P_DRAM)
        par = (u32)data_d;
      else if(c->par == P_VRAM)
        par = (u32)data_v;
      else if(c->par == P_VALUE)
        par = c->val;
      build(jit_d, c);
      grid_u(run_code(jit_d, c->iters, par));
    }

  /* the same sequential runs fetched from VRAM */
  progress(L("CODE EN VRAM", "CODE IN VRAM"));
  v.w0 = NOP4;
  v.w1 = 0;
  v.words = 1;
  v.par = P_NONE;
  v.val = 0;
  v.rep = 16;
  v.iters = 8192;
  build(jit_v, &v);
  grid_u(run_code(jit_v, v.iters, 0));
  v.rep = 1024;
  v.iters = 128;
  build(jit_v, &v);
  grid_u(run_code(jit_v, v.iters, 0));
  grid_end();
  arm_free();
}

/* ---------------------------------------------------------------- random sequences */

static u32 seed;

static u32 rbits(u32 n)
{
  return rand_next(&seed) >> (32 - n);
}

static u32 rreg(u32 n)
{
  return (rand_next(&seed) >> 8) % n;
}

static u32 rcond(void)
{
  return ((rand_next(&seed) >> 8) % 15) << 28;
}

static const u32 pool[16] =
  {
    0x00000000u, 0x00000001u, 0x00000002u, 0x0000001Fu,
    0x00000020u, 0x00000021u, 0x000000FFu, 0x00000100u,
    0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x80000001u,
    0x55555555u, 0xAAAAAAAAu, 0x00010000u, 0xFFFF0000u
  };

static u32 pick(void)
{
  if(rbits(1))
    return pool[rbits(4)];
  return rand_next(&seed);
}

static u32 head(u32 *c)
{
  u32 n;

  n = 0;
  c[n++] = PUSH_ALL;
  c[n++] = 0xE92D0001u;   /* stmfd sp!,{r0} */
  c[n++] = 0xE5901024u;   /* ldr r1,[r0,#36] */
  c[n++] = 0xE128F001u;   /* msr cpsr_f,r1 */
  c[n++] = 0xE5909028u;   /* ldr r9,[r0,#40] */
  c[n++] = 0xE89001FFu;   /* ldmia r0,{r0-r8} */
  return n;
}

static u32 tail(u32 *c, u32 n)
{
  c[n++] = 0xE92D01FFu;   /* stmfd sp!,{r0-r8} */
  c[n++] = 0xE10F1000u;   /* mrs r1,cpsr */
  c[n++] = 0xE59D0024u;   /* ldr r0,[sp,#36] */
  c[n++] = 0xE5801024u;   /* str r1,[r0,#36] */
  c[n++] = 0xE8BD03FEu;   /* ldmfd sp!,{r1-r9} */
  c[n++] = 0xE8BD0001u;   /* ldmfd sp!,{r0} */
  c[n++] = 0xE88003FEu;   /* stmia r0,{r1-r9} */
  c[n++] = POP_ALL;
  return n;
}

/* data processing fields common to the three operand forms */
static u32 dp_fields(u32 op)
{
  u32 s;
  u32 rd;
  u32 rn;

  s  = rbits(1);
  rd = rreg(9);
  rn = rreg(9);
  if(op >= 8 && op <= 11)
    {
      s = 1;
      rd = 0;
    }
  if(op == 13 || op == 15)
    rn = 0;
  return rcond() | (op << 21) | (s << 20) | (rn << 16) | (rd << 12);
}

static u32 gen_imm(u32 group)
{
  return dp_fields((group << 2) | rbits(2)) | 0x02000000u | (rbits(4) << 8) | rbits(8);
}

static u32 gen_shift_imm(u32 type)
{
  return dp_fields(rbits(4)) | (rbits(5) << 7) | (type << 5) | rreg(9);
}

static u32 gen_shift_reg(u32 type)
{
  return dp_fields(rbits(4)) | (rreg(9) << 8) | (type << 5) | 0x10u | rreg(9);
}

static u32 gen_mul(u32 s)
{
  u32 rd;
  u32 rm;

  rd = rreg(9);
  do
    rm = rreg(9);
  while(rm == rd);
  return rcond() | (rbits(1) << 21) | (s << 20) | (rd << 16) | (rreg(9) << 12) | (rreg(9) << 8) | 0x90u | rm;
}

/* version 3.3: the same with the address kept word aligned (any_load: loads excepted).
   The register forms use r7 multiple of 4 under LSL, of 32 under LSR and ASR. */
static u32 gen_mem_aligned(u32 *c, u32 n, u32 any_load)
{
  u32 load;
  u32 reg;
  u32 amount;
  u32 type;
  u32 imm;
  u32 op;
  u32 free_addr;

  load   = rbits(1);
  reg    = rbits(1);
  amount = rbits(2);
  type   = rreg(3);
  free_addr = any_load && load;
  if(free_addr)
    imm = rbits(6);
  else if(reg && type != 0)
    imm = rbits(3) << 5;
  else
    imm = rbits(3) << 2;
  c[n++] = 0xE1A08009u;                     /* mov r8,r9 */
  c[n++] = 0xE3A07000u | imm;               /* mov r7,#imm */
  op  = rcond();
  op |= 0x04000000u;
  op |= rbits(1) << 24;                     /* P */
  op |= rbits(1) << 23;                     /* U */
  op |= rbits(1) << 21;                     /* W */
  op |= load << 20;
  op |= 8u << 16;
  op |= rreg(7) << 12;
  if(reg)
    op |= 0x02000000u | (amount << 7) | (type << 5) | 7u;
  else if(free_addr)
    op |= rbits(6);
  else
    op |= rbits(4) << 2;
  c[n++] = op;
  return n;
}

/* emits: base in r8, small offset in r7, then one memory instruction */
static u32 gen_mem(u32 *c, u32 n, u32 byte)
{
  u32 op;

  c[n++] = 0xE1A08009u;                                 /* mov r8,r9 */
  c[n++] = 0xE3A07000u | rbits(6);                      /* mov r7,#small */
  op = rcond() | 0x04000000u | (rbits(1) << 24) | (rbits(1) << 23) | (byte << 22) |
    (rbits(1) << 21) | (rbits(1) << 20) | (8u << 16) | (rreg(7) << 12);
  if(rbits(1))
    op |= 0x02000000u | (rbits(2) << 7) | (rreg(3) << 5) | 7u;
  else
    op |= rbits(6);
  c[n++] = op;
  return n;
}

static u32 gen_block(u32 *c, u32 n, u32 load)
{
  u32 list;
  u32 wb;

  list = rbits(8);
  if(list == 0)
    list = 1;
  wb = rbits(1);
  if(load && wb == 0 && rbits(1))
    list |= 0x100u;
  c[n++] = 0xE1A08009u;
  c[n++] = rcond() | 0x08000000u | (rbits(1) << 24) | (rbits(1) << 23) | (wb << 21) |
    (load << 20) | (8u << 16) | list;
  return n;
}

static u32 gen_swp(u32 *c, u32 n)
{
  c[n++] = 0xE1A08009u;
  c[n++] = rcond() | 0x01000090u | (rbits(1) << 22) | (8u << 16) | (rreg(7) << 12) | rreg(7);
  return n;
}

static u32 gen_psr(u32 *c, u32 n)
{
  u32 k;

  k = rbits(2);
  if(k == 0)
    c[n++] = rcond() | 0x010F0000u | (rreg(9) << 12);                  /* mrs */
  else if(k == 1)
    c[n++] = rcond() | 0x0128F000u | rreg(9);                          /* msr cpsr_f,reg */
  else
    c[n++] = rcond() | 0x0328F000u | (rbits(4) << 8) | rbits(8);       /* msr cpsr_f,#imm */
  return n;
}

#define CAT_MUL   12
#define CAT_MULS  13
#define CAT_LDR   14
#define CAT_LDRB  15
#define CAT_LDM   16
#define CAT_STM   17
#define CAT_SWP   18
#define CAT_PSR   19
#define CAT_MIX   20
#define NCATS     21
#define CAT_LDRA  21   /* version 3.3: LDR and STR, always word aligned */
#define CAT_LDRU  22   /* version 3.3: stores aligned, loads at any address */

static u32 gen_one(u32 *c, u32 n, u32 cat)
{
  u32 k;

  if(cat < 4)
    c[n++] = gen_imm(cat);
  else if(cat < 8)
    c[n++] = gen_shift_imm(cat - 4);
  else if(cat < 12)
    c[n++] = gen_shift_reg(cat - 8);
  else
    switch(cat)
      {
      case CAT_MUL:  c[n++] = gen_mul(0); break;
      case CAT_MULS: c[n++] = gen_mul(1); break;
      case CAT_LDR:  n = gen_mem(c, n, 0); break;
      case CAT_LDRB: n = gen_mem(c, n, 1); break;
      case CAT_LDM:  n = gen_block(c, n, 1); break;
      case CAT_STM:  n = gen_block(c, n, 0); break;
      case CAT_SWP:  n = gen_swp(c, n); break;
      case CAT_PSR:  n = gen_psr(c, n); break;
      case CAT_LDRA: n = gen_mem_aligned(c, n, 0); break;
      case CAT_LDRU: n = gen_mem_aligned(c, n, 1); break;
      default:
        k = rbits(3);
        if(k < 2)
          c[n++] = gen_imm(rbits(2));
        else if(k < 4)
          c[n++] = gen_shift_imm(rbits(2));
        else if(k < 6)
          c[n++] = gen_shift_reg(rbits(2));
        else if(k == 6)
          c[n++] = gen_mul(rbits(1));
        else
          n = gen_psr(c, n);
        break;
      }
  return n;
}

/* version 3.1: shows which category and which program of it is about to run; slow mode
   leaves each one on the screen for a whole field */
static int arm_show;
static int arm_slow;
static volatile u32 arm_run;   /* version 3.3: read by the task while a thread works */

static void arm_step(u32 cat, u32 r)
{
  char line[LINE_CHARS];
  char *p;

  p = put_s(line, "CAT ");
  p = put_u(p, cat, 2);
  p = put_s(p, " RUN ");
  p = put_u(p, r, 4);
  p = put_s(p, "             ");
  (void)p;
  draw_row(0, 21, line, MakeRGB15(31, 31, 0));
  if(arm_slow)
    WaitVBL(b_vbl_ioreq, 1);
}

static u32 run_cat(u32 cat, u32 runs, u32 ins)
{
  static u32 s[11];
  u32 crc;
  u32 r;
  u32 i;
  u32 n;
  int mem;

  mem = ((cat >= CAT_LDR && cat <= CAT_SWP) || cat == CAT_LDRA || cat == CAT_LDRU);
  seed = 0x5EED0000u + cat;
  crc = 0;
  for(i = 0; i < SCR_BYTES; i++)
    scratch0[i] = (u8)(rand_next(&seed) >> 24);
  for(r = 0; r < runs; r++)
    {
      arm_run = r;
      if(arm_show && (arm_slow || (r & 15) == 0))
        arm_step(cat, r);
      n = head(jit_d);
      for(i = 0; i < ins; i++)
        n = gen_one(jit_d, n, cat);
      if(mem)
        jit_d[n++] = 0xE3A08000u;   /* mov r8,#0: its value depends on where the scratch area is */
      (void)tail(jit_d, n);
      for(i = 0; i < 9; i++)
        s[i] = pick();
      s[9]  = rand_next(&seed) & 0xF0000000u;
      s[10] = (u32)(scratch + SCR_MID);
      if(mem)
        memcpy(scratch, scratch0, SCR_BYTES);
      ((JitState)jit_d)(s);
      s[9] &= 0xF0000000u;
      crc = crc32_update(crc, (const u8 *)s, 40);
      if(mem)
        crc = crc32_update(crc, scratch + SCR_MID - 280, 560);
    }
  return crc;
}

/* where the value of the program counter is read from */
static void pc_reads(void)
{
  u32 *c;
  u32 *m;
  u32 n;

  c = jit_d;
  m = (u32 *)scratch;
  n = 0;
  c[n++] = PUSH_ALL;
  c[n++] = 0xE3A07000u;   /* mov r7,#0 */
  c[n++] = 0xE3A08000u;   /* mov r8,#0 */
  c[n++] = 0xE1A0400Fu;   /* 3: mov r4,pc */
  c[n++] = 0xE28F5000u;   /* 4: add r5,pc,#0 */
  c[n++] = 0xE08F6817u;   /* 5: add r6,pc,r7,lsl r8 */
  c[n++] = 0xE581F000u;   /* 6: str pc,[r1] */
  c[n++] = 0xE8828000u;   /* 7: stmia r2,{pc} */
  c[n++] = 0xE8830070u;   /* stmia r3,{r4-r6} */
  c[n++] = POP_ALL;
  (void)n;
  ((JitFn)jit_d)(0, (u32)m, (u32)(m + 1), (u32)(m + 4));
  grid_u(m[4] - (u32)&c[3]);
  grid_u(m[5] - (u32)&c[4]);
  grid_u(m[6] - (u32)&c[5]);
  grid_u(m[0] - (u32)&c[6]);
  grid_u(m[1] - (u32)&c[7]);
}

/* the flags left by muls: C is documented as meaningless, this shows what the chip does */
static void muls_flags(const char *tag, u32 flags, u32 m)
{
  static u32 s[11];
  char *p;
  u32 *c;
  u32 n;
  u32 k;
  u32 i;

  p = new_line(tag);
  for(k = 0; k < 32; k++)
    {
      c = jit_d;
      n = head(c);
      c[n++] = 0xE0140695u;   /* muls r4,r5,r6 */
      (void)tail(c, n);
      for(i = 0; i < 9; i++)
        s[i] = 0;
      s[5] = m;
      s[6] = 1u << k;
      s[9] = flags;
      s[10] = 0;
      ((JitState)c)(s);
      p = put_x(p, s[9] >> 28, 1);
    }
  (void)p;
  log_line();
}

void group_arm_crc(void)
{
  u32 cat;
  u32 runs;
  u32 ins;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('C', 4);
  for(cat = 0; cat < NCATS; cat++)
    {
      progress("SEQUENCES");
      runs = (cat >= CAT_LDR && cat <= CAT_SWP) ? 150 : 1000;
      ins  = 12;
      grid_x(run_cat(cat, runs, ins), 8);
    }
  grid_end();
  progress("PC");
  grid_begin('D', 5);
  pc_reads();
  grid_end();
  muls_flags("FA ", 0, 0x7FFFFFFFu);
  muls_flags("FB ", 0x30000000u, 0x7FFFFFFFu);
  muls_flags("FC ", 0, 0x80000001u);
  muls_flags("FD ", 0x20000000u, 0xFFFFFFFFu);
  arm_free();
}

/* The families of the page above from a chosen one, each result shown as soon as it is
   known, to find which family stops the real console. */
void group_arm_crc3(void)
{
  char line[LINE_CHARS];
  char *p;
  u32 buttons;
  u32 cat;
  u32 first;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  first = CAT_MUL;
  arm_slow = 0;
  for(;;)
    {
      p = put_s(line, L("DEPART FAMILLE ", "START FAMILY   "));
      p = put_u(p, first, 2);
      p = put_s(p, arm_slow ? L("  LENT   ", "  SLOW   ") : "  NORMAL ");
      (void)p;
      draw_row(0, 2, line, MakeRGB15(31, 31, 31));
      draw_row(0, 3, L("GAUCHE DROITE: FAMILLE", "LEFT RIGHT: FAMILY    "), MakeRGB15(18, 18, 18));
      draw_row(0, 4, L("HAUT: LENT OU NORMAL", "UP: SLOW OR NORMAL  "), MakeRGB15(18, 18, 18));
      draw_row(0, 5, L("A: LANCER  X: MENU", "A: START  X: MENU "), MakeRGB15(18, 18, 18));
      buttons = wait_buttons(ControlLeft | ControlRight | ControlUp | ControlA | ControlStart | ControlX);
      if(buttons & ControlX)
        {
          arm_free();
          return;
        }
      if(buttons & ControlLeft)
        first = (first + NCATS - 1) % NCATS;
      if(buttons & ControlRight)
        first = (first + 1) % NCATS;
      if(buttons & ControlUp)
        arm_slow = !arm_slow;
      if(buttons & (ControlA | ControlStart))
        break;
    }
  draw_row(0, 3, "                              ", 0);
  draw_row(0, 4, "                              ", 0);
  draw_row(0, 5, "                              ", 0);
  muls_flags("FA ", 0, 0x7FFFFFFFu);
  muls_flags("FB ", 0x30000000u, 0x7FFFFFFFu);
  muls_flags("FC ", 0, 0x80000001u);
  muls_flags("FD ", 0x20000000u, 0xFFFFFFFFu);
  arm_show = 1;
  grid_begin('C', 2);
  for(cat = first; cat < NCATS; cat++)
    grid_x(run_cat(cat, (cat >= CAT_LDR && cat <= CAT_SWP) ? 150 : 1000, 12), 8);
  grid_end();
  arm_show = 0;
  arm_slow = 0;
  arm_free();
}

/* version 3.2: the programs of the memory family one instruction at a time, the last one
   shown before it runs: the photo of the last screen says which instruction stops the console */
void group_arm_step(void)
{
  static u32 s[11];
  static u32 prog[64];
  static u32 ends[13];
  static u32 regs[9];
  char line[LINE_CHARS];
  char *p;
  u32 buttons;
  u32 cat;
  u32 fl;
  u32 r;
  u32 i;
  u32 j;
  u32 k;
  u32 n;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  cat = CAT_LDR;
  seed = 0x5EED0000u + cat;
  for(i = 0; i < SCR_BYTES; i++)
    scratch0[i] = (u8)(rand_next(&seed) >> 24);
  for(r = 0; r < 150; r++)
    {
      n = 0;
      ends[0] = 0;
      for(i = 0; i < 12; i++)
        {
          n = gen_one(prog, n, cat);
          ends[i + 1] = n;
        }
      for(i = 0; i < 9; i++)
        regs[i] = pick();
      fl = rand_next(&seed) & 0xF0000000u;
      for(k = 1; k <= 12; k++)
        {
          p = put_s(line, "R ");
          p = put_u(p, r, 3);
          p = put_s(p, " I ");
          p = put_u(p, k, 2);
          p = put_s(p, " ");
          p = put_x(p, prog[ends[k] - 2], 8);
          p = put_s(p, " ");
          p = put_x(p, prog[ends[k] - 1], 8);
          p = put_s(p, "     ");
          (void)p;
          draw_row(0, 21, line, MakeRGB15(31, 31, 0));
          WaitVBL(b_vbl_ioreq, 1);
          n = head(jit_d);
          for(j = 0; j < ends[k]; j++)
            jit_d[n++] = prog[j];
          jit_d[n++] = 0xE3A08000u;
          (void)tail(jit_d, n);
          for(i = 0; i < 9; i++)
            s[i] = regs[i];
          s[9] = fl;
          s[10] = (u32)(scratch + SCR_MID);
          memcpy(scratch, scratch0, SCR_BYTES);
          ((JitState)jit_d)(s);
        }
      buttons = 0;
      DoControlPad(1, &buttons, 0);
      if(buttons & ControlX)
        break;
    }
  text_line((r == 150) ? L("FIN SANS ARRET", "END, NO STOP  ") : L("ARRET PAR X", "ENDED BY X "));
  arm_free();
}

/* ---------------------------------------------------------------- version 3.3 */

/* MULS DRAPEAU C: what C holds after muls and mlas, read directly. Each row is one value
   of Rs and three words of 32 bits, one bit for each position j:
     C after muls with Rm = 1 << j, C = 0 before
     C after muls with Rm = 1 << j, C = 1 before
     C after muls with Rm = ~(1 << j), C = 0 before
   Page 1, grid M, Rs = 0 .. 15, 10, 100.
   Page 2, grid P, Rs = the multipliers of the table lists below.
   Page 3, grid S: mlas with Rn = 0 for 4 values of Rs, mlas with Rn = FFFFFFFF for the same,
   muls r6,r5,r6 (Rd = Rs) for 2 values, then 12 words of muls and 12 words of mlas: one
   random Rs per word, one random Rm (and Rn) per bit. */
#define MULS_456  0xE0140695u  /* muls r4,r5,r6 */
#define MLAS_456  0xE0347695u  /* mlas r4,r5,r6,r7 */
#define MULS_656  0xE0160695u  /* muls r6,r5,r6 */

static void mc_build(u32 op)
{
  u32 n;

  n = head(jit_d);
  jit_d[n++] = op;
  (void)tail(jit_d, n);
}

static u32 mc_carry(u32 flags, u32 rm, u32 rs, u32 rn)
{
  static u32 s[11];
  u32 i;

  for(i = 0; i < 9; i++)
    s[i] = 0;
  s[5] = rm;
  s[6] = rs;
  s[7] = rn;
  s[9] = flags;
  s[10] = 0;
  ((JitState)jit_d)(s);
  return (s[9] >> 29) & 1;
}

static void mc_line(u32 rs, u32 rn)
{
  u32 j;
  u32 m0;
  u32 m1;
  u32 m2;

  m0 = 0;
  m1 = 0;
  m2 = 0;
  for(j = 0; j < 32; j++)
    {
      m0 |= mc_carry(0x00000000u, 1u << j, rs, rn) << j;
      m1 |= mc_carry(0x20000000u, 1u << j, rs, rn) << j;
      m2 |= mc_carry(0x00000000u, ~(1u << j), rs, rn) << j;
    }
  grid_x(m0, 8);
  grid_x(m1, 8);
  grid_x(m2, 8);
}

/* one word: a random Rs (of a random size) and 32 random Rm */
static void mc_random(u32 with_rn)
{
  u32 rs;
  u32 sh;
  u32 rm;
  u32 rn;
  u32 j;
  u32 m;

  rs = rand_next(&seed);
  sh = rand_next(&seed) >> 27;
  rs >>= sh;
  m = 0;
  for(j = 0; j < 32; j++)
    {
      rm = rand_next(&seed);
      rn = 0;
      if(with_rn)
        rn = rand_next(&seed);
      m |= mc_carry(0, rm, rs, rn) << j;
    }
  grid_x(m, 8);
}

void group_muls(int page)
{
  static const u32 p2[18] =
    {
      0x00000055u, 0x000000AAu, 0x000000FFu, 0x0000FFFFu, 0x00010000u, 0x10000000u,
      0x20000000u, 0x30000000u, 0x40000000u, 0x60000000u, 0x80000000u, 0xA0000000u,
      0xC0000000u, 0xE0000000u, 0xF0000000u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0xAAAAAAAAu
    };
  static const u32 p3[4] = { 0x00000000u, 0x00000001u, 0x00000100u, 0xC0000000u };
  u32 i;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress("MULS");
  if(page == 0)
    {
      grid_begin('M', 3);
      mc_build(MULS_456);
      for(i = 0; i < 16; i++)
        mc_line(i, 0);
      mc_line(0x10, 0);
      mc_line(0x100, 0);
    }
  else if(page == 1)
    {
      grid_begin('P', 3);
      mc_build(MULS_456);
      for(i = 0; i < 18; i++)
        mc_line(p2[i], 0);
    }
  else
    {
      grid_begin('S', 3);
      mc_build(MLAS_456);
      for(i = 0; i < 4; i++)
        mc_line(p3[i], 0);
      for(i = 0; i < 4; i++)
        mc_line(p3[i], 0xFFFFFFFFu);
      mc_build(MULS_656);
      mc_line(0x00000055u, 0);
      mc_line(0xFFFFFFFFu, 0);
      seed = 0x4D554C53u;
      mc_build(MULS_456);
      for(i = 0; i < 12; i++)
        mc_random(0);
      mc_build(MLAS_456);
      for(i = 0; i < 12; i++)
        mc_random(1);
    }
  grid_end();
  arm_free();
}

/* Work that can be stopped by the memory controller runs in its own thread: the kernel
   kills only the task at fault (the bit ABT_BWACC of MADAM, "byte address, word access",
   is one of the abort causes), and the page goes on. */
#define TH_OK     0
#define TH_ABORT  1
#define TH_LATE   2
#define TH_FAIL   3
#define TH_STACK  8192
#define TH_FIELDS 7200        /* two minutes */
#define TH_NOCAT  0xFFFFFFFFu

static volatile u32 th_done;
static volatile u32 th_crc;
static u32 th_cat;
static u32 th_runs;
static u32 th_s[11];

static void th_family(void)
{
  th_crc = run_cat(th_cat, th_runs, 12);
  th_done = 1;
}

static void th_case(void)
{
  ((JitState)jit_d)(th_s);
  th_done = 1;
}

static u32 th_run(void (*fn)(void), u32 cat)
{
  Item t;
  u32 n;

  th_done = 0;
  arm_run = 0;
  t = CreateThread("p2i", CURRENTTASK->t.n_Priority, fn, TH_STACK);
  if(t < 0)
    return TH_FAIL;
  for(n = 0; n < TH_FIELDS; n++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      if(th_done)
        return TH_OK;
      if(LookupItem(t) == NULL)
        return TH_ABORT;
      if(cat != TH_NOCAT && (n & 7) == 0)
        arm_step(cat, arm_run);
    }
  DeleteThread(t);
  return TH_LATE;
}

/* the first of count choices, chosen with Left and Right; returns -1 for X */
static int ask_first(const char *what, u32 count)
{
  char line[LINE_CHARS];
  char *p;
  u32 buttons;
  u32 first;

  first = 0;
  for(;;)
    {
      p = put_s(line, L("DEPART ", "START  "));
      p = put_s(p, what);
      p = put_s(p, " ");
      p = put_u(p, first, 2);
      p = put_s(p, "     ");
      (void)p;
      draw_row(0, 2, line, MakeRGB15(31, 31, 31));
      draw_row(0, 3, L("GAUCHE DROITE: CHANGER", "LEFT RIGHT: CHANGE    "), MakeRGB15(18, 18, 18));
      draw_row(0, 4, L("A: LANCER  X: MENU", "A: START  X: MENU "), MakeRGB15(18, 18, 18));
      buttons = wait_buttons(ControlLeft | ControlRight | ControlA | ControlStart | ControlX);
      if(buttons & ControlX)
        return -1;
      if(buttons & ControlLeft)
        first = (first + count - 1) % count;
      if(buttons & ControlRight)
        first = (first + 1) % count;
      if(buttons & (ControlA | ControlStart))
        break;
    }
  draw_row(0, 2, "                              ", 0);
  draw_row(0, 3, "                              ", 0);
  draw_row(0, 4, "                              ", 0);
  return (int)first;
}

/* ARM CRC ALIGNE, grid R, two families per row: LDR aligned, LDRB, LDM, STM, SWP, PSR, MIX.
   DEAD0000 + n: the family was stopped at run n; DEAF: still running after two minutes;
   DEAC: the thread could not be created. */
void group_arm_aligned(int ask)
{
  static const u8 fam[7] = { CAT_LDRA, CAT_LDRB, CAT_LDM, CAT_STM, CAT_SWP, CAT_PSR, CAT_MIX };
  int first;
  u32 i;
  u32 r;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  first = 0;
  if(ask)
    {
      first = ask_first(L("FAMILLE", "FAMILY "), 7);
      if(first < 0)
        {
          arm_free();
          return;
        }
    }
  grid_begin('R', 2);
  for(i = (u32)first; i < 7; i++)
    {
      th_cat  = fam[i];
      th_runs = (th_cat == CAT_PSR || th_cat == CAT_MIX) ? 1000 : 150;
      r = th_run(th_family, th_cat);
      if(r == TH_OK)
        grid_x(th_crc, 8);
      else if(r == TH_ABORT)
        grid_x(0xDEAD0000u | arm_run, 8);
      else if(r == TH_LATE)
        grid_x(0xDEAF0000u | arm_run, 8);
      else
        grid_x(0xDEAC0000u, 8);
    }
  grid_end();
  arm_free();
}

/* ARM NON ALIGNE: one instruction per line, base r8, data r1. Columns: case number, OK or
   AB (the thread was stopped) or TT (no end), then r1 after the instruction and the words
   at the aligned address and the next one. Memory before: 11121314 21222324 (the word
   before is 01020304); r1 before: A1A2A3A4 (loads and stores alike), r2 before: B1B2B3B4.
   The last line, FAM, is the CRC of the family with aligned stores and loads anywhere. */
#define LDR_R1   0xE5981000u  /* ldr r1,[r8] */
#define LDRB_R1  0xE5D81000u  /* ldrb r1,[r8] */
#define STR_R1   0xE5881000u  /* str r1,[r8] */
#define STRB_R1  0xE5C81000u  /* strb r1,[r8] */
#define LDM_R1   0xE8980002u  /* ldmia r8,{r1} */
#define STM_R1   0xE8880002u  /* stmia r8,{r1} */
#define SWP_R1   0xE1081092u  /* swp r1,r2,[r8] */

typedef struct NaCase
{
  u32 op;
  u8  off;
  u8  vram;
} NaCase;

static const NaCase na_cases[] =
  {
    { LDR_R1,  0, 0 },   /*  0 controls: nothing should stop */
    { LDRB_R1, 1, 0 },   /*  1 */
    { STRB_R1, 1, 0 },   /*  2 */
    { STR_R1,  0, 0 },   /*  3 */
    { LDR_R1,  1, 0 },   /*  4 */
    { LDR_R1,  2, 0 },   /*  5 */
    { LDR_R1,  3, 0 },   /*  6 */
    { STR_R1,  1, 0 },   /*  7 */
    { STR_R1,  2, 0 },   /*  8 */
    { STR_R1,  3, 0 },   /*  9 */
    { LDM_R1,  1, 0 },   /* 10 */
    { STM_R1,  1, 0 },   /* 11 */
    { SWP_R1,  1, 0 },   /* 12 */
    { LDR_R1,  1, 1 },   /* 13 VRAM */
    { STR_R1,  1, 1 }    /* 14 VRAM */
  };

#define NA_COUNT (sizeof(na_cases) / sizeof(na_cases[0]))

void group_unaligned(void)
{
  char line[LINE_CHARS];
  char *p;
  int first;
  u32 i;
  u32 n;
  u32 r;
  u32 *w;

  if(arm_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  first = ask_first(L("CAS", "NO."), NA_COUNT);
  if(first < 0)
    {
      arm_free();
      return;
    }
  for(i = (u32)first; i < NA_COUNT; i++)
    {
      p = put_s(line, L("CAS ", "NO. "));
      p = put_u(p, i, 2);
      p = put_s(p, L(" EN COURS            ", " RUNNING             "));
      (void)p;
      draw_row(0, 21, line, MakeRGB15(31, 31, 0));
      WaitVBL(b_vbl_ioreq, 1);

      w = na_cases[i].vram ? (data_v + 256) : (u32 *)(scratch + SCR_MID);
      w[-1] = 0x01020304u;
      w[0]  = 0x11121314u;
      w[1]  = 0x21222324u;
      w[2]  = 0x31323334u;

      n = head(jit_d);
      jit_d[n++] = na_cases[i].op;
      (void)tail(jit_d, n);
      for(n = 0; n < 9; n++)
        th_s[n] = 0;
      th_s[1]  = 0xA1A2A3A4u;
      th_s[2]  = 0xB1B2B3B4u;
      th_s[8]  = (u32)w + na_cases[i].off;
      th_s[9]  = 0;
      th_s[10] = 0;
      r = th_run(th_case, TH_NOCAT);

      p = new_line("");
      p = put_u(p, i, 2);
      if(r == TH_OK)
        {
          p = put_s(p, " OK ");
          p = put_x(p, th_s[1], 8);
          p = put_s(p, " ");
          p = put_x(p, w[0], 8);
          p = put_s(p, " ");
          p = put_x(p, w[1], 8);
        }
      else if(r == TH_ABORT)
        p = put_s(p, " AB");
      else if(r == TH_LATE)
        p = put_s(p, " TT");
      else
        p = put_s(p, " ER");
      (void)p;
      log_line();
    }

  progress(L("FAMILLE", "FAMILY "));
  th_cat  = CAT_LDRU;
  th_runs = 150;
  r = th_run(th_family, th_cat);
  p = new_line("FAM ");
  if(r == TH_OK)
    {
      p = put_s(p, "OK ");
      p = put_x(p, th_crc, 8);
    }
  else
    {
      p = put_s(p, (r == TH_ABORT) ? "AB RUN " : ((r == TH_LATE) ? "TT RUN " : "ER RUN "));
      p = put_u(p, arm_run, 0);
    }
  (void)p;
  log_line();
  arm_free();
}
