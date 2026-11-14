/*
  P2i Lab 3DO bench, group CPU DETAIL: ARM60 timings for multiplies, DRAM and VRAM
  loads and stores, calls, divisions, branches and system calls. Each line is the
  time of the whole loop in microseconds.
*/

#include "bench.h"

#include "mem.h"
#include "string.h"
#include "time.h"

#define LOOPS        1000000
#define BUF_BYTES    65536
#define BUF_WORDS    (BUF_BYTES / 4)
#define PASSES       (LOOPS / BUF_WORDS)   /* 61 passes of 16K words: about 1M accesses */
#define CHASE_STEPS  262144
#define DIVS         100000
#define SWIS         10000

static volatile u32 small_factor = 3;
static volatile u32 big_factor = 0x7FFFFFF3;

static u32 mul_loop(u32 n, u32 m)
{
  u32 x;

  x = 1;
  while(n != 0)
    {
      x = x * m + n;
      n--;
    }
  return x;
}

static u32 sum_words(const u32 *p, u32 words, u32 passes)
{
  u32 s;
  u32 i;

  s = 0;
  while(passes-- != 0)
    for(i = 0; i < words; i++)
      s += p[i];
  return s;
}

static void store_words(u32 *p, u32 words, u32 passes)
{
  u32 i;

  while(passes-- != 0)
    for(i = 0; i < words; i++)
      p[i] = i;
}

static u32 chase(const u32 *next, u32 steps)
{
  u32 i;

  i = 0;
  while(steps-- != 0)
    i = next[i];
  return i;
}

static u32 tiny(u32 x)
{
  return x + 1;
}

static u32 calls(u32 (*f)(u32), u32 n)
{
  u32 x;

  x = 0;
  while(n-- != 0)
    x = f(x);
  return x;
}

static u32 divisions(u32 n)
{
  u32 s;
  u32 d;

  s = 0;
  d = 7;
  while(n != 0)
    {
      s += 0xFFFFFFF0 / d + n / d;
      d = d * 3 + 1;
      if(d > 100000)
        d = 7;
      n--;
    }
  return s;
}

static u32 branches(u32 n)
{
  u32 x;

  x = 0;
  while(n != 0)
    {
      if(((n * 2654435761u) >> 31) != 0)
        x += n;
      else
        x ^= n;
      n--;
    }
  return x;
}

static u32 swis(u32 n)
{
  u32 x;

  x = 0;
  while(n-- != 0)
    x += SampleSystemTime();
  return x;
}

void group_cpu(void)
{
  u32 *dram;
  u32 *vram;
  u32 i;
  u32 j;
  u32 k;
  u32 seed;
  Clock ck;
  u32 t;

  dram = (u32 *)AllocMem(BUF_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL);
  vram = (u32 *)AllocMem(BUF_BYTES, MEMTYPE_VRAM | MEMTYPE_FILL);
  if(dram == NULL || vram == NULL)
    {
      if(dram != NULL)
        FreeMem(dram, BUF_BYTES);
      if(vram != NULL)
        FreeMem(vram, BUF_BYTES);
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }

  clock_start(&ck);
  b_sink ^= mul_loop(LOOPS, small_factor);
  t = clock_us(&ck);
  value_line("MUL X3 1M ", t);

  clock_start(&ck);
  b_sink ^= mul_loop(LOOPS, big_factor);
  t = clock_us(&ck);
  value_line("MUL XG 1M ", t);

  clock_start(&ck);
  b_sink ^= sum_words(dram, BUF_WORDS, PASSES);
  t = clock_us(&ck);
  value_line("LDR DRAM  ", t);

  clock_start(&ck);
  store_words(dram, BUF_WORDS, PASSES);
  t = clock_us(&ck);
  value_line("STR DRAM  ", t);

  clock_start(&ck);
  b_sink ^= sum_words(vram, BUF_WORDS, PASSES);
  t = clock_us(&ck);
  value_line("LDR VRAM  ", t);

  clock_start(&ck);
  store_words(vram, BUF_WORDS, PASSES);
  t = clock_us(&ck);
  value_line("STR VRAM  ", t);

  clock_start(&ck);
  for(i = 0; i < 16; i++)
    memcpy(vram, dram, BUF_BYTES);
  t = clock_us(&ck);
  value_line("CPY D>V 1M", t);

  clock_start(&ck);
  for(i = 0; i < 16; i++)
    memcpy(dram, vram, BUF_BYTES);
  t = clock_us(&ck);
  value_line("CPY V>D 1M", t);

  /* one random cycle through the 16K words (Sattolo), then follow it */
  for(i = 0; i < BUF_WORDS; i++)
    dram[i] = i;
  seed = 12345;
  for(i = BUF_WORDS - 1; i > 0; i--)
    {
      j = rand_next(&seed) % i;
      k = dram[i];
      dram[i] = dram[j];
      dram[j] = k;
    }
  clock_start(&ck);
  b_sink ^= chase(dram, CHASE_STEPS);
  t = clock_us(&ck);
  value_line(L("LDR ALEA  ", "LDR RAND  "), t);

  clock_start(&ck);
  b_sink ^= calls(tiny, LOOPS);
  t = clock_us(&ck);
  value_line(L("APPELS 1M ", "CALLS 1M  "), t);

  clock_start(&ck);
  b_sink ^= divisions(DIVS);
  t = clock_us(&ck);
  value_line("DIV 100K  ", t);

  clock_start(&ck);
  b_sink ^= branches(LOOPS);
  t = clock_us(&ck);
  value_line(L("SAUTS 1M  ", "BRANCH 1M "), t);

  clock_start(&ck);
  b_sink ^= swis(SWIS);
  t = clock_us(&ck);
  value_line("SWI 10K   ", t);

  FreeMem(dram, BUF_BYTES);
  FreeMem(vram, BUF_BYTES);
}

/* ---------------------------------------------------------------- version 3.0 */

#define MEM_BYTES  262144
#define MEM_MASK   (MEM_BYTES - 1)
#define MEM_READS  32768
#define MEM_REPS   3

static const u16 strides[12] = { 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

static u32 read_stride(const u8 *base, u32 stride)
{
  u32 n;
  u32 ofs;
  u32 s;

  s = 0;
  ofs = 0;
  for(n = MEM_READS; n != 0; n--)
    {
      s += *(const u32 *)(base + ofs);
      ofs = (ofs + stride) & MEM_MASK;
    }
  return s;
}

static void write_stride(u8 *base, u32 stride)
{
  u32 n;
  u32 ofs;

  ofs = 0;
  for(n = MEM_READS; n != 0; n--)
    {
      *(u32 *)(base + ofs) = n;
      ofs = (ofs + stride) & MEM_MASK;
    }
}

static void stride_row(u8 *base, int write)
{
  static Stat s;
  u32 i;
  u32 r;
  u32 t0;

  for(i = 0; i < 12; i++)
    {
      stat_reset(&s);
      for(r = 0; r < MEM_REPS; r++)
        {
          t0 = usec_now();
          if(write)
            write_stride(base, strides[i]);
          else
            b_sink ^= read_stride(base, strides[i]);
          stat_add(&s, usec_now() - t0);
        }
      grid_u(stat_median(&s));
    }
}

/* page size and row change cost: the same number of accesses with growing strides,
   in four groups of twelve (DRAM read, DRAM write, VRAM read, VRAM write) */
void group_mem(void)
{
  u8 *dram;
  u8 *vram;

  dram = (u8 *)AllocMem(MEM_BYTES, MEMTYPE_DRAM | MEMTYPE_FILL);
  vram = (u8 *)AllocMem(MEM_BYTES, MEMTYPE_VRAM | MEMTYPE_FILL);
  if(dram == NULL || vram == NULL)
    {
      if(dram != NULL)
        FreeMem(dram, MEM_BYTES);
      if(vram != NULL)
        FreeMem(vram, MEM_BYTES);
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('M', 4);
  progress(L("DRAM LECTURE", "DRAM READ   "));
  stride_row(dram, 0);
  progress(L("DRAM ECRITURE", "DRAM WRITE   "));
  stride_row(dram, 1);
  progress(L("VRAM LECTURE", "VRAM READ   "));
  stride_row(vram, 0);
  progress(L("VRAM ECRITURE", "VRAM WRITE   "));
  stride_row(vram, 1);
  grid_end();
  FreeMem(dram, MEM_BYTES);
  FreeMem(vram, MEM_BYTES);
}
