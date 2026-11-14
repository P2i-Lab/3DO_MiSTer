/*
  P2i Lab 3DO bench 3.0, group DSP: the arithmetic of the audio DSP, seen through the
  stock instruments. Each instrument's knobs are set to all the combinations of eight
  values and its output is read with a probe once it has settled. A line gives the CRC32
  of all the outputs and the output for a few inputs.
*/

#include "bench.h"

#include "audio.h"
#include "string.h"

#define NVALS   8
#define SETTLE  300     /* microseconds: several audio frames */

static const s32 vals[NVALS] = { 0, 1, -1, 0x7FFF, -0x8000, 0x4000, -0x4000, 0x1234 };
static char *const knob_names[3] = { "InputA", "InputB", "InputC" };

typedef struct DspTest
{
  const char *name;
  char *file;
  int knobs;
} DspTest;

static const DspTest tests[6] =
  {
    { "TIMESP ", "timesplus.dsp", 3 },
    { "MULT   ", "multiply.dsp",  2 },
    { "ADD    ", "add.dsp",       2 },
    { "SUB    ", "subtract.dsp",  2 },
    { "MAX    ", "maximum.dsp",   2 },
    { "MIN    ", "minimum.dsp",   2 }
  };

static void err_line(const char *name, Err e)
{
  char *p;

  p = new_line(name);
  p = put_s(p, L("ERREUR ", "ERROR  "));
  p = put_x(p, (u32)e, 8);
  (void)p;
  log_line();
}

static void settle(void)
{
  u32 t0;

  t0 = usec_now();
  while(usec_now() - t0 < SETTLE)
    ;
}

static void run_test(const DspTest *t)
{
  Item ins;
  Item probe;
  Item knob[3];
  s32 v;
  u32 be;
  u32 crc;
  u32 count;
  u32 unstable;
  u32 a;
  u32 b;
  u32 c;
  u32 cmax;
  s32 first;
  s32 sample1;
  s32 sample2;
  s32 cycles;
  int i;
  char *p;

  ins = LoadInstrument(t->file, 0, 100);
  if(ins < 0)
    {
      err_line(t->name, ins);
      return;
    }
  for(i = 0; i < t->knobs; i++)
    knob[i] = GrabKnob(ins, knob_names[i]);
  probe = CreateProbe(ins, "Output", NULL);
  if(probe < 0)
    {
      err_line(t->name, probe);
      UnloadInstrument(ins);
      return;
    }
  StartInstrument(ins, NULL);
  settle();
  cycles = GetAudioCyclesUsed();

  crc = 0;
  count = 0;
  unstable = 0;
  sample1 = 0;
  sample2 = 0;
  cmax = (t->knobs == 3) ? NVALS : 1;
  for(a = 0; a < NVALS; a++)
    for(b = 0; b < NVALS; b++)
      for(c = 0; c < cmax; c++)
        {
          TweakRawKnob(knob[0], vals[a]);
          TweakRawKnob(knob[1], vals[b]);
          if(t->knobs == 3)
            TweakRawKnob(knob[2], vals[c]);
          settle();
          v = 0;
          first = 0;
          ReadProbe(probe, &first);
          settle();
          ReadProbe(probe, &v);
          if(v != first)
            unstable++;
          be = (u32)v;
          crc = crc32_update(crc, (const u8 *)&be, 4);
          count++;
          if(a == 5 && b == 5 && c == 0)
            sample1 = v;
          if(a == 3 && b == 3 && c == 0)
            sample2 = v;
        }
  StopInstrument(ins, NULL);
  DeleteProbe(probe);
  for(i = 0; i < t->knobs; i++)
    ReleaseKnob(knob[i]);
  UnloadInstrument(ins);

  p = new_line(t->name);
  p = put_x(p, crc, 8);
  p = put_u(p, count, 4);
  p = put_u(p, unstable, 4);
  p = put_s(p, " ");
  p = put_x(p, (u32)sample1, 4);
  p = put_s(p, " ");
  p = put_x(p, (u32)sample2, 4);
  p = put_u(p, (u32)cycles, 5);
  (void)p;
  log_line();
}

void group_dsp(void)
{
  int i;

  for(i = 0; i < 6; i++)
    {
      progress(tests[i].name);
      run_test(&tests[i]);
    }
}
