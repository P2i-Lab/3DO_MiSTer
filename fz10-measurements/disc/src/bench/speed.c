/*
  P2i Lab 3DO bench, group VITESSE: the first measurements (version 1).
  VBL period, CPU loops, memcpy, VRAM clear, Strahl's cels, a display loop like
  Strahl's, other cels, a 512 KB file read twice.
*/

#include "bench.h"

#include "blockfile.h"
#include "mem.h"
#include "string.h"

#define CEL_REPS     30
#define LOOP_ITERS   60
#define SMALL_REPS   10
#define VRAM_REPS    30
#define CPU_LOOPS    1000000
#define COPY_BYTES   65536
#define COPY_REPS    16           /* 16 x 64 KB = 1 MB */
#define CD_FILE      "$boot/bench_data/cd512k.bin"
#define CD_BYTES     524288

static void test_vbl(void)
{
  Clock ck;
  u32 v0;
  u32 t;
  u32 v;
  char *p;

  progress("VBL");
  WaitVBL(b_vbl_ioreq, 1);
  v0 = vbl_now();
  clock_start(&ck);
  WaitVBL(b_vbl_ioreq, 120);
  t = clock_us(&ck);
  v = vbl_now() - v0;
  p = new_line("VBL X120  ");
  p = put_u(p, t, 8);
  p = put_s(p, " US ");
  p = put_u(p, v, 4);
  p = put_s(p, " VBL");
  (void)p;
  log_line();
}

static u32 cpu_reg_loop(u32 n)
{
  u32 x;

  x = 1;
  while(n != 0)
    {
      x = x * 5 + n;
      n--;
    }
  return x;
}

static void cpu_mem_loop(u32 n)
{
  while(n != 0)
    {
      b_sink += n;
      n--;
    }
}

static void test_cpu(void)
{
  Clock ck;
  u32 t;

  progress("CPU REG");
  clock_start(&ck);
  b_sink ^= cpu_reg_loop(CPU_LOOPS);
  t = clock_us(&ck);
  value_line("CPU REG 1M", t);

  progress("CPU MEM");
  clock_start(&ck);
  cpu_mem_loop(CPU_LOOPS);
  t = clock_us(&ck);
  value_line("CPU MEM 1M", t);
}

static void test_copy(void)
{
  u8 *a;
  u8 *b;
  u32 i;
  Clock ck;
  u32 t;

  progress("MEMCPY");
  a = (u8 *)AllocMem(COPY_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL | 0x5A);
  b = (u8 *)AllocMem(COPY_BYTES, MEMTYPE_DRAM);
  if(a == NULL || b == NULL)
    {
      if(a != NULL)
        FreeMem(a, COPY_BYTES);
      if(b != NULL)
        FreeMem(b, COPY_BYTES);
      text_line(L("MEMCPY: PAS DE MEMOIRE", "MEMCPY: OUT OF MEMORY "));
      return;
    }
  clock_start(&ck);
  for(i = 0; i < COPY_REPS; i++)
    memcpy(b, a, COPY_BYTES);
  t = clock_us(&ck);
  FreeMem(a, COPY_BYTES);
  FreeMem(b, COPY_BYTES);
  value_line("MEMCPY 1MB", t);
}

static void test_vram(void)
{
  static Stat s;
  u32 i;
  u32 t0;

  progress("VRAM CLR");
  stat_reset(&s);
  for(i = 0; i < VRAM_REPS; i++)
    {
      t0 = usec_now();
      SetVRAMPages(b_vram_ioreq, b_sc.sc_Bitmaps[1]->bm_Buffer, 0,
                   (s32)b_sc.sc_NumBitmapPages, 0xFFFFFFFF);
      stat_add(&s, usec_now() - t0);
    }
  stat_line("VRAM CLR  ", &s);
}

/* fill + slide drawn and shown, then wait for a VBL, like a game loop */
static void test_loop(void)
{
  u32 i;
  Clock ck;
  u32 v0;
  u32 t;
  u32 v;
  int cur;
  char *p;

  progress(L("BOUCLE", "LOOP  "));
  cur = 1;
  WaitVBL(b_vbl_ioreq, 1);
  v0 = vbl_now();
  clock_start(&ck);
  for(i = 0; i < LOOP_ITERS; i++)
    {
      DrawCels(b_sc.sc_BitmapItems[cur], &b_ccbs[C_FILL]);
      DrawCels(b_sc.sc_BitmapItems[cur], &b_ccbs[C_IMAGE]);
      DisplayScreen(b_sc.sc_Screens[cur], 0);
      WaitVBL(b_vbl_ioreq, 1);
      cur = 1 - cur;
    }
  t = clock_us(&ck);
  v = vbl_now() - v0;
  DisplayScreen(b_sc.sc_Screens[0], 0);
  p = new_line(L("BOUCLE X60", "LOOP X60  "));
  p = put_u(p, t, 8);
  p = put_s(p, " US ");
  p = put_u(p, v, 4);
  p = put_s(p, " VBL");
  (void)p;
  log_line();
}

static u32 check_cd(const u8 *buf)
{
  u32 i;
  u32 bad;

  bad = 0;
  for(i = 0; i < CD_BYTES; i++)
    if(buf[i] != (u8)(i * 13 + (i >> 11)))
      bad++;
  return bad;
}

/* reads a 512 KB file twice; the first read includes the seek */
static void test_cd(void)
{
  u8 *buf;
  s32 size;
  Clock ck;
  u32 t1;
  u32 t2;
  u32 bad;
  char *p;

  progress("CD 512K");
  buf = (u8 *)AllocMem(CD_BYTES, MEMTYPE_DRAM);
  if(buf == NULL)
    {
      text_line(L("CD: PAS DE MEMOIRE", "CD: OUT OF MEMORY "));
      return;
    }
  size = 0;
  clock_start(&ck);
  if(LoadFileHere(CD_FILE, &size, buf, CD_BYTES) == NULL)
    {
      FreeMem(buf, CD_BYTES);
      text_line(L("CD: FICHIER ILLISIBLE", "CD: UNREADABLE FILE  "));
      return;
    }
  t1 = clock_us(&ck);
  clock_start(&ck);
  LoadFileHere(CD_FILE, &size, buf, CD_BYTES);
  t2 = clock_us(&ck);
  bad = check_cd(buf);
  FreeMem(buf, CD_BYTES);
  value_line("CD 512K 1 ", t1);
  p = new_line("CD 512K 2 ");
  p = put_u(p, t2, 8);
  p = put_s(p, " US ERR ");
  p = put_u(p, bad, 1);
  (void)p;
  log_line();
}

void group_speed(void)
{
  test_vbl();
  test_cpu();
  test_copy();
  test_vram();
  time_cels("CEL 1PX   ", &b_ccbs[C_DOT], NULL, CEL_REPS);
  time_cels(L("CEL FOND  ", "CEL BKGND "), &b_ccbs[C_FILL], NULL, CEL_REPS);
  time_cels("CEL IMAGE ", &b_ccbs[C_IMAGE], NULL, CEL_REPS);
  time_cels("STRAHL F+I", &b_ccbs[C_FILL], &b_ccbs[C_IMAGE], CEL_REPS);
  test_loop();
  time_cels("CEL 16BPP ", &b_ccbs[C_16BPP], NULL, CEL_REPS);
  time_cels("CEL 8BPP  ", &b_ccbs[C_8BPP], NULL, CEL_REPS);
  time_cels(L("CEL DEMI  ", "CEL HALF  "), &b_ccbs[C_HALF], NULL, CEL_REPS);
  time_cels("CEL 8X8 1K", &b_ccbs[C_SMALL], NULL, SMALL_REPS);
  test_cd();
}
