/*
  P2i Lab 3DO bench, groups MATERIEL and REGISTRES.

  MATERIEL: chip revisions and settings, CRC32 of the two ROMs (to know which BIOS
  the console has), free memory, the video line counter (lines per field, field bit,
  the line where a task wakes up after WaitVBL), and the audio clock against the
  system timer and the VBL. Chip registers are read through trapexit's svc_mem driver.

  REGISTRES: raw dumps of MADAM and CLIO registers, 32 per page. The CD interface
  (CLIO 0x400 and up) and the DSP are not read.
*/

#include "bench.h"

#include "audio.h"
#include "controlpad.h"
#include "event.h"
#include "graphics.h"
#include "hardware_addrs.h"
#include "kernel.h"
#include "list.h"
#include "mem.h"
#include "string.h"
#include "svc_mem.h"

#define ROM_BYTES    (1024 * 1024)
#define CHUNK_BYTES  65536
#define SAMPLES      400
#define WAKES        30
#define AUDIO_VBLS   300

static Item dev = -1;
static int dev_tried;
static Err dev_err;
static u32 samples[SAMPLES];

void hw_close(void)
{
  if(dev >= 0)
    {
      svc_mem_close_device(dev);
      svc_mem_destroy();
    }
  dev = -1;
  dev_tried = 0;
}

static int hw_open(void)
{
  Err err;

  if(!dev_tried)
    {
      dev_tried = 1;
      err = svc_mem_init();
      if(err < 0)
        {
          dev_err = err;
          return -1;
        }
      dev = svc_mem_open_device();
      if(dev < 0)
        dev_err = dev;
    }
  return (dev >= 0) ? 0 : -1;
}

static u32 rd(u32 base, u32 byte_offset)
{
  u32 val;

  val = 0xDEADDEAD;
  svc_mem_r_u32(dev, (u32 *)base, (i32)(byte_offset / 4), &val, 1);
  return val;
}

static void hex2_line(const char *n1, u32 v1, const char *n2, u32 v2)
{
  char *p;

  p = new_line(n1);
  p = put_x(p, v1, 8);
  p = put_s(p, " ");
  p = put_s(p, n2);
  p = put_x(p, v2, 8);
  (void)p;
  log_line();
}

/* CRC32 of a whole ROM read in one go, as trapexit's read_rom example does; the
   result can be compared with known BIOS dumps (CRC32 of 1 MB of zeros: A738EA1C) */
static void rom_line(const char *name, int which)
{
  u8 *buf;
  Err err;
  char *p;

  progress(name);
  p = new_line(name);
  buf = (u8 *)AllocMem(ROM_BYTES, MEMTYPE_DRAM);
  if(buf == NULL)
    {
      p = put_s(p, L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      (void)p;
      log_line();
      return;
    }
  memset(buf, 0, ROM_BYTES);
  if(which == 1)
    err = svc_mem_r_u32_rom1(dev, 0, (u32 *)buf, ROM_BYTES / 4);
  else
    err = svc_mem_r_u32_rom2(dev, 0, (u32 *)buf, ROM_BYTES / 4);
  if(err < 0)
    {
      p = put_s(p, L("ERREUR ", "ERROR  "));
      p = put_x(p, (u32)err, 8);
    }
  else
    {
      p = put_s(p, "CRC ");
      p = put_x(p, crc32_update(0, buf, ROM_BYTES), 8);
      p = put_s(p, " ");
      p = put_x(p, ((u32)buf[0] << 24) | ((u32)buf[1] << 16) | ((u32)buf[2] << 8) | buf[3], 8);
    }
  (void)p;
  log_line();
  FreeMem(buf, ROM_BYTES);
}

static void free_memory(void)
{
  MemInfo mi;
  char *p;

  p = new_line(L("LIBRE DRAM", "FREE DRAM "));
  AvailMem(&mi, MEMTYPE_DRAM);
  p = put_u(p, mi.minfo_SysFree + mi.minfo_TaskFree, 8);
  p = put_s(p, " VRAM");
  AvailMem(&mi, MEMTYPE_VRAM);
  p = put_u(p, mi.minfo_SysFree + mi.minfo_TaskFree, 8);
  (void)p;
  log_line();
}

/* line counter: range, field bit, cost of one read; then the line after WaitVBL */
static void video_counters(void)
{
  u32 i;
  Clock ck;
  u32 t;
  u32 vmin;
  u32 vmax;
  u32 f0;
  u32 f1;
  u32 hmin;
  u32 hmax;
  u32 x;
  static Stat s;
  char *p;

  progress("VCNT");
  clock_start(&ck);
  for(i = 0; i < SAMPLES; i++)
    samples[i] = rd(CLIO_ADDR, 0x34);
  t = clock_us(&ck);
  vmin = 0xFFFFFFFF;
  vmax = 0;
  f0 = 0;
  f1 = 0;
  for(i = 0; i < SAMPLES; i++)
    {
      x = samples[i] & 0x7FF;
      if(x < vmin)
        vmin = x;
      if(x > vmax)
        vmax = x;
      if(samples[i] & 0x800)
        f1++;
      else
        f0++;
    }
  p = new_line("VCNT      ");
  p = put_u(p, vmin, 4);
  p = put_s(p, "-");
  p = put_u(p, vmax, 0);
  p = put_s(p, " FLD ");
  p = put_u(p, f0, 0);
  p = put_s(p, "/");
  p = put_u(p, f1, 0);
  (void)p;
  log_line();

  p = new_line(L("LECT SVC  ", "SVC READ  "));
  p = put_u(p, t / SAMPLES, 4);
  p = put_s(p, L(" US  BRUT ", " US  RAW: "));
  p = put_x(p, samples[0], 8);
  (void)p;
  log_line();

  progress("HCNT");
  for(i = 0; i < SAMPLES; i++)
    samples[i] = rd(CLIO_ADDR, 0x30);
  hmin = 0xFFFFFFFF;
  hmax = 0;
  for(i = 0; i < SAMPLES; i++)
    {
      x = samples[i] & 0xFFFF;
      if(x < hmin)
        hmin = x;
      if(x > hmax)
        hmax = x;
    }
  p = new_line("HCNT      ");
  p = put_u(p, hmin, 4);
  p = put_s(p, "-");
  p = put_u(p, hmax, 0);
  (void)p;
  log_line();

  progress(L("LIGNE VBL", "VBL LINES"));
  stat_reset(&s);
  for(i = 0; i < WAKES; i++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      stat_add(&s, rd(CLIO_ADDR, 0x34) & 0x7FF);
    }
  stat_line(L("LIGNE VBL ", "VBL LINES "), &s);
}

/* audio frames and audio ticks counted over AUDIO_VBLS fields, against the timer */
static void audio_clock(void)
{
  u32 i;
  Clock ck;
  u32 t;
  u32 v0;
  u32 v;
  u32 frames;
  u16 last;
  u16 now;
  AudioTime a0;
  AudioTime a;
  char *p;

  progress("AUDIO 5S");
  WaitVBL(b_vbl_ioreq, 1);
  v0 = vbl_now();
  clock_start(&ck);
  a0 = GetAudioTime();
  last = GetAudioFrameCount();
  frames = 0;
  for(i = 0; i < AUDIO_VBLS; i++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      now = GetAudioFrameCount();
      frames += (u16)(now - last);
      last = now;
    }
  t = clock_us(&ck);
  a = GetAudioTime() - a0;
  v = vbl_now() - v0;

  p = new_line("5S        ");
  p = put_u(p, t, 8);
  p = put_s(p, " US ");
  p = put_u(p, v, 4);
  p = put_s(p, " VBL");
  (void)p;
  log_line();

  p = new_line("AUDIO     ");
  p = put_u(p, frames, 8);
  p = put_s(p, L(" TR ", " FRM"));
  p = put_u(p, (u32)a, 5);
  p = put_s(p, " TK");
  (void)p;
  log_line();
}

void group_hw(void)
{
  char *p;

  /* the ROM2 CRC needs 1 MB: the cel data is freed and set up again after */
  cels_free();

  progress(L("MEMOIRE LIBRE", "  FREE MEMORY"));
  free_memory();
  audio_clock();

  progress("SVC_MEM");
  if(hw_open() < 0)
    {
      p = new_line(L("SVC_MEM ERREUR ", "SVC_MEM ERROR  "));
      p = put_x(p, (u32)dev_err, 8);
      (void)p;
      log_line();
      return;
    }

  hex2_line("MADAM ", rd(MADAM_ADDR, 0x00), "CLIO ", rd(CLIO_ADDR, 0x00));
  hex2_line("MSYS  ", rd(MADAM_ADDR, 0x04), "MCTL ", rd(MADAM_ADDR, 0x08));
  hex2_line("VINT0 ", rd(CLIO_ADDR, 0x08), "VIN1 ", rd(CLIO_ADDR, 0x0C));
  hex2_line("HDLY  ", rd(CLIO_ADDR, 0x80), "CSTA ", rd(CLIO_ADDR, 0x28));
  video_counters();
  rom_line("ROM1 ", 1);
  rom_line("ROM2 ", 2);
}

/* page 0-1: MADAM 000-0FC, page 2: CLIO 000-07C, page 3: CLIO 080-0BC and timers 100-13C */
int group_regs(int page)
{
  u32 base;
  u32 first;
  u32 i;
  u32 off;
  char *p;

  progress("SVC_MEM");
  if(hw_open() < 0)
    {
      p = new_line(L("SVC_MEM ERREUR ", "SVC_MEM ERROR  "));
      p = put_x(p, (u32)dev_err, 8);
      (void)p;
      log_line();
      return 4;
    }
  base = (page < 2) ? MADAM_ADDR : CLIO_ADDR;
  first = (page == 1) ? 0x80 : 0;
  p = NULL;
  for(i = 0; i < 32; i++)
    {
      off = first + i * 4;
      if(page == 3)
        off = (i < 16) ? 0x80 + i * 4 : 0x100 + (i - 16) * 4;
      if((i & 1) == 0)
        {
          p = new_line((page < 2) ? "M" : "C");
          p = put_x(p, off, 3);
        }
      else
        {
          p = put_s(p, "  ");
          p = put_x(p, off, 3);
        }
      p = put_s(p, " ");
      p = put_x(p, rd(base, off), 8);
      if(i & 1)
        log_line();
    }
  return 4;
}

/* ---------------------------------------------------------------- version 3.0 */

#define WATCH_READS  128
#define CLOCK_VBLS   3600

typedef struct Watch
{
  const char *name;
  u32 base;
  u32 off;
} Watch;

static const Watch watches[16] =
  {
    { "M008 ", MADAM_ADDR, 0x008 },
    { "M00C ", MADAM_ADDR, 0x00C },
    { "M040 ", MADAM_ADDR, 0x040 },
    { "M010 ", MADAM_ADDR, 0x010 },
    { "C024 ", CLIO_ADDR,  0x024 },
    { "C03C ", CLIO_ADDR,  0x03C },
    { "C084 ", CLIO_ADDR,  0x084 },
    { "C088 ", CLIO_ADDR,  0x088 },
    { "C030 ", CLIO_ADDR,  0x030 },
    { "C034 ", CLIO_ADDR,  0x034 },
    { "C040 ", CLIO_ADDR,  0x040 },
    { "C060 ", CLIO_ADDR,  0x060 },
    { "C100 ", CLIO_ADDR,  0x100 },
    { "C108 ", CLIO_ADDR,  0x108 },
    { "C118 ", CLIO_ADDR,  0x118 },
    { "C130 ", CLIO_ADDR,  0x130 }
  };

static int need_dev(void)
{
  char *p;

  progress("SVC_MEM");
  if(hw_open() < 0)
    {
      p = new_line(L("SVC_MEM ERREUR ", "SVC_MEM ERROR  "));
      p = put_x(p, (u32)dev_err, 8);
      (void)p;
      log_line();
      return -1;
    }
  return 0;
}

/* which bits of a register ever change, and how often it changes between two reads */
void group_regtime(void)
{
  u32 w;
  u32 i;
  u32 v;
  u32 last;
  u32 and_v;
  u32 or_v;
  u32 changes;
  char *p;

  if(need_dev() < 0)
    return;
  for(w = 0; w < 16; w++)
    {
      progress(watches[w].name);
      last = rd(watches[w].base, watches[w].off);
      and_v = last;
      or_v = last;
      changes = 0;
      for(i = 1; i < WATCH_READS; i++)
        {
          v = rd(watches[w].base, watches[w].off);
          and_v &= v;
          or_v |= v;
          if(v != last)
            changes++;
          last = v;
        }
      p = new_line(watches[w].name);
      p = put_x(p, and_v, 8);
      p = put_s(p, " ");
      p = put_x(p, or_v, 8);
      p = put_s(p, " ");
      p = put_x(p, last, 8);
      p = put_u(p, changes, 4);
      (void)p;
      log_line();
    }
}

static int scan_room;
static char *scan_p;
static int scan_col;
static u32 scan_hidden;

static void scan_put(u32 off, u32 v)
{
  if(scan_room == 0)
    {
      scan_hidden++;
      return;
    }
  if(scan_col == 0)
    scan_p = new_line("");
  else
    scan_p = put_s(scan_p, " ");
  scan_p = put_x(scan_p, off, 3);
  scan_p = put_s(scan_p, " ");
  scan_p = put_x(scan_p, v, 8);
  scan_col++;
  if(scan_col == 3)
    {
      log_line();
      scan_col = 0;
      scan_room--;
    }
}

static void scan_range(u32 base, u32 first, u32 last, u32 hide)
{
  u32 off;
  u32 v;

  for(off = first; off <= last; off += 4)
    {
      v = rd(base, off);
      if(v != 0 && v != hide)
        scan_put(off, v);
    }
}

/* the registers not shown by REGISTRES, only those that are neither 0 nor the value
   read from an address known to be empty: page 0 MADAM 100-7FC, page 1 CLIO 140-3FC */
void group_scan(int page)
{
  u32 hide;
  char *p;

  if(need_dev() < 0)
    return;
  hide = rd(MADAM_ADDR, 0x0F0);
  scan_room = 17;
  scan_col = 0;
  scan_hidden = 0;
  progress(L("LECTURE", "READING"));
  if(page == 0)
    scan_range(MADAM_ADDR, 0x100, 0x7FC, hide);
  else
    scan_range(CLIO_ADDR, 0x140, 0x3FC, hide);
  if(scan_col != 0)
    log_line();
  p = new_line(L("NON AFFICHES ", "NOT SHOWN    "));
  p = put_u(p, scan_hidden, 0);
  p = put_s(p, L(" MASQUE ", " MASK   "));
  p = put_x(p, hide, 8);
  (void)p;
  log_line();
}

static void sys_line(const char *n1, u32 v1, const char *n2, u32 v2)
{
  char *p;

  p = new_line(n1);
  p = put_x(p, v1, 8);
  p = put_s(p, " ");
  p = put_s(p, n2);
  p = put_x(p, v2, 8);
  (void)p;
  log_line();
}

static void node_list(List *l)
{
  Node *n;
  char *p;
  int count;

  count = 0;
  p = NULL;
  for(n = FirstNode(l); IsNode(l, n); n = NextNode(n))
    {
      if(count == 0)
        p = new_line("");
      else
        p = put_s(p, " ");
      p = put_s(p, (n->n_Name != NULL) ? n->n_Name : "?");
      p = put_u(p, ((const ItemNode *)n)->n_Version, 3);
      p = put_s(p, ".");
      p = put_u(p, ((const ItemNode *)n)->n_Revision, 0);
      count++;
      if(count == 2)
        {
          log_line();
          count = 0;
        }
    }
  if(count != 0)
    log_line();
}

void group_system(void)
{
  sys_line("CPUFLAG ", KernelBase->kb_CPUFlags, "FLAGS ", KernelBase->kb_Flags);
  sys_line("TICKS N ", KernelBase->kb_numticks, "D ", KernelBase->kb_denomticks);
  sys_line("TIMERS  ", KernelBase->kb_TimerBits, "QUANT ", KernelBase->kb_ElapsedQuanta);
  sys_line("ITEMS   ", (u32)KernelBase->kb_MaxItem, "INT ", KernelBase->kb_MaxInterrupts);
  sys_line("MADAM   ", KernelBase->kb_MadamRev, "CLIO ", KernelBase->kb_ClioRev);
  sys_line("DRAMSET ", KernelBase->kb_DRAMSetSize, "VRAM ", KernelBase->kb_VRAMSetSize);
  sys_line("VRAM0   ", KernelBase->kb_VRAM0Size, "VRAM1 ", KernelBase->kb_VRAM1Size);
  sys_line("MEMEND  ", KernelBase->kb_MemEnd, "SWITCH ", KernelBase->kb_NumTaskSwitches);
  sys_line("PAGE D  ", (u32)GetPageSize(MEMTYPE_DRAM), "V ", (u32)GetPageSize(MEMTYPE_VRAM));
  node_list(KernelBase->kb_FolioList);
  node_list(KernelBase->kb_Devices);
}

/* ---------------------------------------------------------------- control port */

#define PB_WORDS     28
#define GUN_TARGETS  9
#define GUN_FRAMES   240     /* fields per target */
#define GUN_SAMPLE   150     /* first field sampled */
#define GUN_WINDOW   200     /* the whole input buffer of the control port */

static u32 pb_snap[PB_WORDS];

/* MADAM's control port DMA: address reached (the data received ends there), length left,
   address of the other buffer */
static void pb_dump(u32 addr, char tag, u32 words)
{
  u32 i;

  memcpy(pb_snap, (const void *)addr, words * 4);
  grid_begin(tag, 4);
  for(i = 0; i < words; i++)
    grid_x(pb_snap[i], 8);
  grid_end();
}

/* registers and raw bytes of the control port, and when the transfer of a field ends */
void group_pbus(void)
{
  static Stat s_time;
  static Stat s_line;
  u32 cur;
  u32 next;
  u32 r;
  u32 i;
  u32 t0;
  char *p;

  if(need_dev() < 0)
    return;
  WaitVBL(b_vbl_ioreq, 2);
  cur  = rd(MADAM_ADDR, 0x570);
  next = rd(MADAM_ADDR, 0x578);
  hex2_line("570 ", cur, "574 ", rd(MADAM_ADDR, 0x574));
  hex2_line("578 ", next, "57C ", rd(MADAM_ADDR, 0x57C));
  hex2_line("MCTL ", rd(MADAM_ADDR, 0x008), "INT ", rd(CLIO_ADDR, 0x040));
  pb_dump(next, 'A', 8);
  pb_dump(cur - PB_WORDS * 4, 'B', PB_WORDS);

  progress(L("FIN DE TRANSFERT", "END OF TRANSFER "));
  stat_reset(&s_time);
  stat_reset(&s_line);
  for(r = 0; r < 16; r++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      t0 = usec_now();
      for(i = 0; i < 60; i++)
        if((rd(MADAM_ADDR, 0x008) & 0x8000) == 0)
          {
            stat_add(&s_time, usec_now() - t0);
            stat_add(&s_line, rd(CLIO_ADDR, 0x034) & 0x7FF);
            break;
          }
    }
  p = new_line(L("FIN US ", "END US "));
  p = put_u(p, stat_median(&s_time), 6);
  p = put_s(p, L(" LIGNE ", " LINE  "));
  p = put_u(p, stat_median(&s_line), 4);
  p = put_s(p, " N ");
  p = put_u(p, s_time.n, 3);
  (void)p;
  log_line();
}

/* Where the input buffer of the control port starts. The OS driver writes the buffer
   address at MADAM 0x570 and 196 at 0x574 (200 bytes less 4) at each field; the address
   grows as the length falls, so their sum gives the start whenever they are read. The
   two registers are read in one call so that they belong together. */
static u32 pb_in_start(void)
{
  u32 r[2];

  r[0] = 0;
  r[1] = 0;
  svc_mem_r_u32(dev, (u32 *)MADAM_ADDR, 0x570 / 4, r, 2);
  return r[0] + r[1] - (GUN_WINDOW - 4);
}

static int pb_in_valid(u32 a)
{
  return (a & 3) == 0 && a >= 0x1000 && a < 0x800000;
}

static u32 fill_white[4];
static u32 fill_black[4];

static void gun_rect(Item bm, u32 *src, s32 x, s32 y, u32 w, u32 h)
{
  CCB c;

  c = b_ccbs[C_FILL];
  c.ccb_SourcePtr = (CelData *)src;
  c.ccb_XPos = (Coord)(x * 65536);
  c.ccb_YPos = (Coord)(y * 65536);
  c.ccb_HDX  = (s32)(w << 20);
  c.ccb_VDY  = (s32)(h << 16);
  DrawCels(bm, &c);
}

/* where the light gun's identifier is in the control port data, or -1 */
static int gun_find(const u8 *b)
{
  int i;

  for(i = 0; i < GUN_WINDOW - 8; i++)
    if(b[i] == 0x4D)
      return i;
  return -1;
}

/* A white screen with a thin cross, at nine places. The player holds the trigger, aimed
   at the cross. For each place the page gives the distance of the gun's data from the
   end of the buffer, its first 8 bytes, and the counter and hit count as the bytes are
   laid out in the emulator: a sample with a hit if there was one, else the last one. */
void group_gun(void)
{
  static u8 snap[GUN_WINDOW];
  static u8 shots[GUN_TARGETS][8];
  static u32 where[GUN_TARGETS];
  static const u16 tx[3] = { 40, 160, 280 };
  static const u16 ty[3] = { 40, 120, 200 };
  Item bm;
  u32 k;
  u32 f;
  u32 x;
  u32 y;
  u32 j;
  u32 a;
  int at;
  int hit;
  char line[LINE_CHARS];
  char *p;

  if(need_dev() < 0)
    return;
  fill_white[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_white[2] = (u32)MakeRGB15(31, 31, 31) << 16;
  fill_black[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_black[2] = (u32)MakeRGB15(0, 0, 0) << 16;
  bm = b_sc.sc_BitmapItems[0];

  for(k = 0; k < GUN_TARGETS; k++)
    {
      x = tx[k % 3];
      y = ty[k / 3];
      gun_rect(bm, fill_white, 0, 0, SCREEN_W, SCREEN_H);
      gun_rect(bm, fill_black, (s32)x - 8, (s32)y, 17, 1);
      gun_rect(bm, fill_black, (s32)x, (s32)y - 8, 1, 17);
      p = put_s(line, L("CIBLE ", "TARGET"));
      p = put_u(p, k + 1, 0);
      p = put_s(p, L(" GACHETTE ENFONCEE", " TRIGGER HELD     "));
      (void)p;
      draw_row(0, 0, line, MakeRGB15(31, 31, 31));
      DisplayScreen(b_sc.sc_Screens[0], 0);

      hit = 0;
      where[k] = 0;
      for(f = 0; f < GUN_FRAMES; f++)
        {
          WaitVBL(b_vbl_ioreq, 1);
          if(f < GUN_SAMPLE)
            continue;
          a = pb_in_start();
          if(!pb_in_valid(a))
            continue;
          memcpy(snap, (const void *)a, GUN_WINDOW);
          at = gun_find(snap);
          if(at >= 0 && !hit)
            {
              memcpy(shots[k], snap + at, 8);
              where[k] = (u32)(at + 1);
              if((snap[at + 4] & 31) != 0)
                hit = 1;
            }
        }
    }
  for(k = 0; k < GUN_TARGETS; k++)
    {
      p = new_line("T");
      p = put_u(p, k + 1, 0);
      p = put_s(p, " ");
      if(where[k] == 0)
        p = put_s(p, L("PAS DE PISTOLET", "NO LIGHT GUN   "));
      else
        {
          p = put_u(p, where[k], 3);
          p = put_s(p, " ");
          for(j = 0; j < 8; j++)
            p = put_x(p, shots[k][j], 2);
          p = put_u(p, (((u32)shots[k][1] & 1) << 19) | ((u32)shots[k][2] << 11) |
                    ((u32)shots[k][3] << 3) | ((u32)shots[k][4] >> 5), 8);
          p = put_u(p, (u32)shots[k][4] & 31, 3);
        }
      (void)p;
      log_line();
    }
  /* the first bytes that are not zero in the last sample, in case no gun was recognised */
  for(j = 0; j < GUN_WINDOW && snap[j] == 0; j++)
    ;
  p = new_line(L("NON NUL ", "NONZERO "));
  p = put_u(p, j, 3);
  p = put_s(p, " ");
  for(k = 0; k < 8 && j + k < GUN_WINDOW; k++)
    p = put_x(p, snap[j + k], 2);
  (void)p;
  log_line();
}

/* one second of the input buffer: is there a light gun on the port? */
int gun_present(void)
{
  static u8 b[GUN_WINDOW];
  u32 f;
  u32 a;
  u32 seen;

  if(hw_open() < 0)
    return 0;
  seen = 0;
  for(f = 0; f < 60; f++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      a = pb_in_start();
      if(!pb_in_valid(a))
        continue;
      memcpy(b, (const void *)a, GUN_WINDOW);
      if(gun_find(b) >= 0)
        seen++;
    }
  return seen >= 30;
}

/* one minute against the system timer, the field counter and the audio clock */
void group_clock(void)
{
  u32 i;
  Clock ck;
  u32 t;
  u32 v0;
  u32 v;
  u32 frames;
  u16 last;
  u16 now;
  AudioTime a0;
  AudioTime a;
  u32 prev;
  u32 x;
  u32 step;
  char *p;

  progress(L("PAS DU CHRONO", "CLOCK STEP US"));
  step = 0xFFFFFFFF;
  prev = usec_now();
  for(i = 0; i < 20000; i++)
    {
      x = usec_now();
      if(x > prev && x - prev < step)
        step = x - prev;
      prev = x;
    }

  progress(L("UNE MINUTE", "ONE MINUTE"));
  WaitVBL(b_vbl_ioreq, 1);
  v0 = vbl_now();
  clock_start(&ck);
  a0 = GetAudioTime();
  last = GetAudioFrameCount();
  frames = 0;
  for(i = 0; i < CLOCK_VBLS; i++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      now = GetAudioFrameCount();
      frames += (u16)(now - last);
      last = now;
    }
  t = clock_us(&ck);
  a = GetAudioTime() - a0;
  v = vbl_now() - v0;

  value_line(L("TEMPS     ", "TIME      "), t);
  p = new_line("VBL       ");
  p = put_u(p, v, 8);
  (void)p;
  log_line();
  p = new_line(L("TRAMES    ", "FRAMES    "));
  p = put_u(p, frames, 8);
  (void)p;
  log_line();
  p = new_line("TICKS     ");
  p = put_u(p, (u32)a, 8);
  (void)p;
  log_line();
  p = new_line(L("PAS US    ", "STEP US   "));
  p = put_u(p, step, 8);
  (void)p;
  log_line();
}

/* ---------------------------------------------------------------- version 3.1 */

#define PBL_WORDS    (GUN_WINDOW / 4)
#define VID_SAMPLES  24000
#define SC3_ENTRIES  32
#define SC3_ROWS     (SC3_ENTRIES / 2)
#define SC3_FIRST    3

static u32 pbl_words[PBL_WORDS];

static void white_row(int row, const char *s)
{
  draw_row(0, row, s, MakeRGB15(31, 31, 31));
}

/* The whole input buffer of the control port, refreshed while the page is up: press and
   hold a button, the words that change belong to the pad. X and Start together leave. */
void group_pb_live(void)
{
  u32 a;
  u32 buttons;
  u32 n;
  u32 i;
  u32 w;
  int row;
  char line[LINE_CHARS];
  char *p;

  if(need_dev() < 0)
    return;
  draw_row(0, 21, "X + START: MENU                ", MakeRGB15(31, 31, 0));
  n = 0;
  for(;;)
    {
      WaitVBL(b_vbl_ioreq, 1);
      a = pb_in_start();
      buttons = 0;
      DoControlPad(1, &buttons, 0xFFFFFFFF);
      if((buttons & ControlX) != 0 && (buttons & ControlStart) != 0)
        break;
      if((n & 7) == 0 && pb_in_valid(a))
        {
          memcpy(pbl_words, (const void *)a, GUN_WINDOW);
          p = put_s(line, "IN ");
          p = put_x(p, a, 8);
          p = put_s(p, " PAD ");
          p = put_x(p, buttons, 8);
          (void)p;
          white_row(1, line);
          w = 0;
          for(row = 0; row < 17; row++)
            {
              p = put_u(line, w, 2);
              for(i = 0; i < 3; i++)
                {
                  p = put_s(p, " ");
                  if(w < PBL_WORDS)
                    p = put_x(p, pbl_words[w], 8);
                  else
                    p = put_s(p, "        ");
                  w++;
                }
              (void)p;
              white_row(2 + row, line);
            }
        }
      n++;
    }
}

/* many samples of the line and column counters: the real bounds of a field */
void group_video(void)
{
  u32 v[2];
  u32 i;
  u32 h;
  u32 l;
  u32 f;
  u32 hmin;
  u32 hmax;
  u32 lmin[2];
  u32 lmax[2];
  u32 cnt[2];
  char *p;

  if(need_dev() < 0)
    return;
  hmin = 0xFFFFFFFF;
  hmax = 0;
  for(f = 0; f < 2; f++)
    {
      lmin[f] = 0xFFFFFFFF;
      lmax[f] = 0;
      cnt[f]  = 0;
    }
  progress(L("24000 LECTURES", "24000 READINGS"));
  for(i = 0; i < VID_SAMPLES; i++)
    {
      v[0] = 0;
      v[1] = 0;
      svc_mem_r_u32(dev, (u32 *)CLIO_ADDR, 0x030 / 4, v, 2);
      h = v[0] & 0x7FF;
      l = v[1] & 0x7FF;
      f = (v[1] >> 11) & 1;
      if(h < hmin) hmin = h;
      if(h > hmax) hmax = h;
      if(l < lmin[f]) lmin[f] = l;
      if(l > lmax[f]) lmax[f] = l;
      cnt[f]++;
    }
  p = new_line("HCNT MIN ");
  p = put_u(p, hmin, 5);
  p = put_s(p, " MAX ");
  p = put_u(p, hmax, 5);
  (void)p;
  log_line();
  for(f = 0; f < 2; f++)
    {
      p = new_line(L("TRAME ", "FIELD "));
      p = put_u(p, f, 1);
      p = put_s(p, " MIN ");
      p = put_u(p, lmin[f], 4);
      p = put_s(p, " MAX ");
      p = put_u(p, lmax[f], 4);
      p = put_s(p, " N ");
      p = put_u(p, cnt[f], 6);
      (void)p;
      log_line();
    }
}

static void vl_line(u32 addr, u32 v)
{
  char *p;

  p = new_line("");
  p = put_x(p, addr, 6);
  p = put_s(p, " ");
  p = put_x(p, v, 8);
  (void)p;
  log_line();
}

/* what is read just after the end of the 1 MB of VRAM, and whether it repeats the start */
void group_vram_limit(void)
{
  static const u32 spots[9] =
    {
      0x2FFFF8, 0x2FFFFC, 0x300000, 0x300004, 0x300008,
      0x30000C, 0x3FFFFC, 0x400000, 0x400004
    };
  u32 i;
  u32 same;
  char *p;

  if(need_dev() < 0)
    return;
  progress(L("LECTURES", "READS   "));
  hex2_line("MSYS ", rd(MADAM_ADDR, 0x004), "MCTL ", rd(MADAM_ADDR, 0x008));
  for(i = 0; i < 9; i++)
    vl_line(spots[i], rd(spots[i] & 0xFFF00000u, spots[i] & 0x000FFFFFu));
  same = 0;
  for(i = 0; i < 64; i++)
    if(rd(0x200000, i * 4) == rd(0x300000, i * 4))
      same++;
  p = new_line(L("EGAUX A 200000: ", "EQUAL TO 200000:"));
  p = put_u(p, same, 2);
  p = put_s(p, L(" SUR 64", " OF 64 "));
  (void)p;
  log_line();
  vl_line(0x200000, rd(0x200000, 0));
  vl_line(0x200004, rd(0x200000, 4));
}

void wait_release(void)
{
  u32 b;

  do
    {
      b = 0;
      DoControlPad(1, &b, 0xFFFFFFFF);
      WaitVBL(b_vbl_ioreq, 1);
    }
  while(b != 0);
}

static char sc3_text[SC3_ENTRIES][14];
static u32 sc3_count;

static void sc3_show(void)
{
  char line[LINE_CHARS];
  char *p;
  u32 r;
  u32 e;

  for(r = 0; r < SC3_ROWS; r++)
    {
      e = r * 2;
      p = put_s(line, (e < sc3_count) ? sc3_text[e] : "            ");
      p = put_s(p, "  ");
      p = put_s(p, (e + 1 < sc3_count) ? sc3_text[e + 1] : "            ");
      (void)p;
      white_row(SC3_FIRST + (int)r, line);
    }
}

/* reads one address; the address is on the screen for a whole field before the read */
static void sc3_read(u32 base, const char *name, u32 off, u32 hide)
{
  char line[LINE_CHARS];
  char *p;
  u32 v;

  p = put_s(line, L("LECTURE ", "READ    "));
  p = put_s(p, name);
  p = put_s(p, " ");
  p = put_x(p, off, 3);
  p = put_s(p, "       ");
  (void)p;
  draw_row(0, 21, line, MakeRGB15(31, 31, 0));
  WaitVBL(b_vbl_ioreq, 1);
  v = rd(base, off);
  if(v == 0 || v == hide)
    return;
  if(sc3_count == SC3_ENTRIES)
    sc3_count = 0;
  p = put_x(sc3_text[sc3_count], off, 3);
  p = put_s(p, " ");
  p = put_x(p, v, 8);
  (void)p;
  sc3_count++;
  sc3_show();
}

/* Register scan by hand. Left, Right, Up, Down choose the address; A reads it, B reads
   the next 32 words, C changes of chip, X leaves. If the console restarts, the address on
   the status line is the one that did it. */
void group_scan3(void)
{
  static const char *names[2] = { "MADAM", "CLIO " };
  u32 base[2];
  u32 lo[2];
  u32 hi[2];
  u32 off[2];
  u32 hide;
  u32 buttons;
  u32 k;
  int which;
  char line[LINE_CHARS];
  char *p;

  if(need_dev() < 0)
    return;
  base[0] = MADAM_ADDR; lo[0] = 0x100; hi[0] = 0x7FC; off[0] = 0x264;
  base[1] = CLIO_ADDR;  lo[1] = 0x140; hi[1] = 0x3FC; off[1] = 0x140;
  which = 0;
  hide = rd(MADAM_ADDR, 0x0F0);
  sc3_count = 0;
  for(;;)
    {
      p = put_s(line, names[which]);
      p = put_s(p, " ");
      p = put_x(p, off[which], 3);
      p = put_s(p, L("  A:1 B:32 C:PUCE X:FIN", "  A:1 B:32 C:CHIP X:END"));
      (void)p;
      white_row(1, line);
      draw_row(0, 21, L("CHOISIS L'ADRESSE                 ", "CHOOSE THE ADDRESS                "), MakeRGB15(31, 31, 0));
      buttons = wait_buttons(ControlUp | ControlDown | ControlLeft | ControlRight |
                             ControlA | ControlB | ControlC | ControlX);
      wait_release();
      if(buttons & ControlX)
        return;
      if(buttons & ControlC)
        which ^= 1;
      if((buttons & ControlRight) && off[which] + 4 <= hi[which])
        off[which] += 4;
      if((buttons & ControlLeft) && off[which] >= lo[which] + 4)
        off[which] -= 4;
      if((buttons & ControlDown) && off[which] + 0x40 <= hi[which])
        off[which] += 0x40;
      if((buttons & ControlUp) && off[which] >= lo[which] + 0x40)
        off[which] -= 0x40;
      if(buttons & ControlA)
        {
          sc3_read(base[which], names[which], off[which], hide);
          if(off[which] + 4 <= hi[which])
            off[which] += 4;
        }
      if(buttons & ControlB)
        for(k = 0; k < 32 && off[which] <= hi[which]; k++)
          {
            sc3_read(base[which], names[which], off[which], hide);
            off[which] += 4;
          }
      if(off[which] > hi[which])
        off[which] = hi[which];
    }
}

/* ---------------------------------------------------------------- version 3.4 */

/* VCNT IMAGE: which line of the picture is being scanned when the line counter has a given
   value. Five strips stand right of a ruler. Each frame, when the counter reaches the value of a
   strip, the part of the strip that holds the expected line turns from red to white, in less
   than one line: the strip stays red above the line scanned at that moment and white below it.
   (The value shown for each strip on the last page is the last value read, 3 to 12 lines before.)
   Read where the red ends against the ruler (one mark every 10 lines). At the end of the frame the
   strips go back to red. A ends the test and shows the counter values really read. */
#define VI_STRIPS  5
#define VI_FIRST   232
#define VI_STEP    16
#define VI_RULER   216
#define VI_FRAMES  3600

static void vi_paint(u32 *fb, u32 k, u32 pair0, u32 pair1, u32 v)
{
  u32 *p;
  u32 n;

  p = fb + pair0 * SCREEN_W + VI_FIRST + k * VI_STEP;
  for(n = pair1 - pair0; n != 0; n--)
    {
      p[0] = v;
      p[1] = v;
      p[2] = v;
      p[3] = v;
      p[4] = v;
      p[5] = v;
      p[6] = v;
      p[7] = v;
      p += SCREEN_W;
    }
}

static void vi_pixel(u32 x, u32 y, u32 color)
{
  u16 *fb;

  fb = (u16 *)b_sc.sc_Bitmaps[0]->bm_Buffer;
  fb[(y >> 1) * 2 * SCREEN_W + 2 * x + (y & 1)] = (u16)color;
}

static u32 vi_line(void)
{
  return rd(CLIO_ADDR, 0x34) & 0x7FF;
}

void group_vimg(void)
{
  static const u16 vn[VI_STRIPS] = { 40, 80, 120, 160, 200 };
  u32 *fb;
  u32 red;
  u32 white;
  u32 pair0[VI_STRIPS];
  u32 pair1[VI_STRIPS];
  u32 sum[VI_STRIPS];
  u32 vmin[VI_STRIPS];
  u32 vmax[VI_STRIPS];
  u32 k;
  u32 x;
  u32 y;
  u32 v;
  u32 frame;
  u32 counted;
  u32 buttons;
  u32 t0;
  u32 cost;
  u32 ucost;
  u32 wait;
  int skip;
  char label[8];
  char *p;
  GrafCon gc;

  if(need_dev() < 0)
    return;

  /* cost of one read of the counter */
  t0 = usec_now();
  for(k = 0; k < 64; k++)
    b_sink = vi_line();
  cost = (usec_now() - t0) / 64;
  t0 = usec_now();
  for(k = 0; k < 64; k++)
    b_sink = usec_now();
  ucost = (usec_now() - t0) / 64;

  /* the ruler and the strips */
  fb    = (u32 *)b_sc.sc_Bitmaps[0]->bm_Buffer;
  red   = MakeRGB15(31, 0, 0);
  red   = (red << 16) | red;
  white = MakeRGB15(31, 31, 31);
  white = (white << 16) | white;
  clear_screen(0);
  for(y = 0; y < SCREEN_H; y += 10)
    for(x = VI_RULER + ((y % 50) ? 4 : 0); x < SCREEN_W; x++)
      vi_pixel(x, y, (y % 50) ? MakeRGB15(10, 10, 10) : MakeRGB15(22, 22, 22));
  memset(&gc, 0, sizeof(gc));
  SetFGPen(&gc, MakeRGB15(22, 22, 22));
  SetBGPen(&gc, MakeRGB15(0, 0, 0));
  for(y = 0; y < SCREEN_H; y += 50)
    {
      p = put_u(label, y, 3);
      (void)p;
      MoveTo(&gc, VI_RULER - 26, (y >= 4) ? y - 4 : 0);
      DrawText8(&gc, b_sc.sc_BitmapItems[0], (const u8 *)label);
    }
  draw_row(0, 0, "P2I BENCH 3.4 VCNT IMAGE", MakeRGB15(31, 31, 0));
  draw_row(0, 2, L("COMPTEUR DES BANDES", "STRIPS BY COUNTER  "), MakeRGB15(31, 31, 31));
  draw_row(0, 3, "40 80 120 160 200", MakeRGB15(31, 31, 31));
  draw_row(0, 5, L("HAUT ROUGE BAS BLANC", "RED TOP, WHITE BELOW"), MakeRGB15(31, 31, 31));
  draw_row(0, 6, L("UNE MARQUE: 10 LIGNES", "ONE MARK: 10 LINES   "), MakeRGB15(31, 31, 31));
  draw_row(0, 8, L("PHOTO PUIS A", "PHOTO THEN A"), MakeRGB15(31, 31, 0));
  for(k = 0; k < VI_STRIPS; k++)
    {
      pair0[k] = ((vn[k] > 48) ? (u32)vn[k] - 48 : 0) / 2;
      pair1[k] = ((u32)vn[k] - 4) / 2;
      sum[k]   = 0;
      vmin[k]  = 0xFFFFFFFF;
      vmax[k]  = 0;
      vi_paint(fb, k, pair0[k], pair1[k], red);
    }
  DisplayScreen(b_sc.sc_Screens[0], 0);

  counted = 0;
  skip    = 1;
  for(frame = 0; frame < VI_FRAMES; frame++)
    {
      /* the start of a frame */
      do
        v = vi_line();
      while(v >= 30);
      for(k = 0; k < VI_STRIPS; k++)
        {
          /* a read takes more than 8 lines: read until the counter is close to the value of the
             strip, then count the lines left with the timer */
          do
            {
              v  = vi_line();
              t0 = usec_now();
            }
          while(v + 12 < vn[k]);
          if(v < vn[k])
            {
              wait = ((u32)vn[k] - v) * 63556 / 1000;
              while(usec_now() - t0 < wait)
                ;
            }
          vi_paint(fb, k, pair0[k], pair1[k], white);
          if(!skip)
            {
              sum[k] += v;
              if(v < vmin[k])
                vmin[k] = v;
              if(v > vmax[k])
                vmax[k] = v;
            }
        }
      if(!skip)
        counted++;
      /* below the last strip: back to red */
      do
        v = vi_line();
      while(v < 245);
      for(k = 0; k < VI_STRIPS; k++)
        vi_paint(fb, k, pair0[k], pair1[k], red);
      skip = 0;
      if((frame & 31) == 31)
        {
          /* the frame after a look at the pad may be late: it is not counted */
          buttons = 0;
          DoControlPad(1, &buttons, 0);
          if(buttons & (ControlA | ControlStart | ControlX))
            break;
          skip = 1;
        }
    }
  wait_release();

  p = new_line(L("COMPTEUR LU  MOY MIN MAX", "COUNTER READ AVG MIN MAX"));
  (void)p;
  log_line();
  for(k = 0; k < VI_STRIPS; k++)
    {
      p = new_line(L("BANDE ", "STRIP "));
      p = put_u(p, vn[k], 3);
      p = put_s(p, "     ");
      p = put_u(p, (counted != 0) ? sum[k] / counted : 0, 4);
      p = put_u(p, (counted != 0) ? vmin[k] : 0, 4);
      p = put_u(p, vmax[k], 4);
      (void)p;
      log_line();
    }
  p = new_line(L("IMAGES ", "FRAMES "));
  p = put_u(p, counted, 5);
  (void)p;
  log_line();
  p = new_line(L("LECTURE US ", "READ US    "));
  p = put_u(p, cost, 4);
  p = put_s(p, L("  CHRONO US ", "  CLOCK US  "));
  p = put_u(p, ucost, 3);
  (void)p;
  log_line();
}

/* ---------------------------------------------------------------- version 3.4, two guns */

/* the places of the first two light guns in the control port data, or -1 */
static void gun_find2(const u8 *b, int *g1, int *g2)
{
  int i;

  *g1 = -1;
  *g2 = -1;
  for(i = 0; i < GUN_WINDOW - 8; i++)
    if(b[i] == 0x4D)
      {
        if(*g1 < 0)
          {
            *g1 = i;
            i += 4;
          }
        else
          {
            *g2 = i;
            return;
          }
      }
}

static u32 gun_counter(const u8 *s)
{
  return (((u32)s[1] & 1) << 19) | ((u32)s[2] << 11) | ((u32)s[3] << 3) | ((u32)s[4] >> 5);
}

/* how many light guns answer during one second: 2, 1 or 0 */
int gun_count(void)
{
  static u8 b[GUN_WINDOW];
  u32 f;
  u32 a;
  u32 one;
  u32 two;
  int g1;
  int g2;

  if(hw_open() < 0)
    return 0;
  one = 0;
  two = 0;
  for(f = 0; f < 60; f++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      a = pb_in_start();
      if(!pb_in_valid(a))
        continue;
      memcpy(b, (const void *)a, GUN_WINDOW);
      gun_find2(b, &g1, &g2);
      if(g2 >= 0)
        two++;
      else if(g1 >= 0)
        one++;
    }
  if(two >= 30)
    return 2;
  if(one + two >= 30)
    return 1;
  return 0;
}

/* PISTOLETS 2 JOUEURS: the nine crosses of PISTOLET CIBLES; both players aim at the same
   cross with the trigger held. For each place and each gun: where its data sits in the
   buffer (from 1), its counter and its hit count, from a sample with a hit if there was
   one. Then the first 8 bytes of each gun at the centre cross. */
void group_gun2(void)
{
  static u8 snap[GUN_WINDOW];
  static u8 shots[GUN_TARGETS][2][8];
  static u32 where[GUN_TARGETS][2];
  static const u16 tx[3] = { 40, 160, 280 };
  static const u16 ty[3] = { 40, 120, 200 };
  Item bm;
  u32 k;
  u32 f;
  u32 x;
  u32 y;
  u32 j;
  u32 a;
  u32 g;
  int at[2];
  int hit[2];
  char line[LINE_CHARS];
  char *p;

  if(need_dev() < 0)
    return;
  fill_white[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_white[2] = (u32)MakeRGB15(31, 31, 31) << 16;
  fill_black[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_black[2] = (u32)MakeRGB15(0, 0, 0) << 16;
  bm = b_sc.sc_BitmapItems[0];

  for(k = 0; k < GUN_TARGETS; k++)
    {
      x = tx[k % 3];
      y = ty[k / 3];
      gun_rect(bm, fill_white, 0, 0, SCREEN_W, SCREEN_H);
      gun_rect(bm, fill_black, (s32)x - 8, (s32)y, 17, 1);
      gun_rect(bm, fill_black, (s32)x, (s32)y - 8, 1, 17);
      p = put_s(line, L("CIBLE ", "TARGET"));
      p = put_u(p, k + 1, 0);
      p = put_s(p, L(" LES DEUX GACHETTES", " BOTH TRIGGERS     "));
      (void)p;
      draw_row(0, 0, line, MakeRGB15(31, 31, 31));
      DisplayScreen(b_sc.sc_Screens[0], 0);

      hit[0] = 0;
      hit[1] = 0;
      where[k][0] = 0;
      where[k][1] = 0;
      for(f = 0; f < GUN_FRAMES; f++)
        {
          WaitVBL(b_vbl_ioreq, 1);
          if(f < GUN_SAMPLE)
            continue;
          a = pb_in_start();
          if(!pb_in_valid(a))
            continue;
          memcpy(snap, (const void *)a, GUN_WINDOW);
          gun_find2(snap, &at[0], &at[1]);
          for(g = 0; g < 2; g++)
            if(at[g] >= 0 && !hit[g])
              {
                memcpy(shots[k][g], snap + at[g], 8);
                where[k][g] = (u32)(at[g] + 1);
                if((snap[at[g] + 4] & 31) != 0)
                  hit[g] = 1;
              }
        }
    }
  for(k = 0; k < GUN_TARGETS; k++)
    {
      p = new_line("T");
      p = put_u(p, k + 1, 0);
      for(g = 0; g < 2; g++)
        {
          p = put_s(p, (g == 0) ? " A" : " B");
          if(where[k][g] == 0)
            p = put_s(p, "   -");
          else
            {
              p = put_u(p, where[k][g], 4);
              p = put_u(p, gun_counter(shots[k][g]), 8);
              p = put_u(p, (u32)shots[k][g][4] & 31, 3);
            }
        }
      (void)p;
      log_line();
    }
  for(g = 0; g < 2; g++)
    {
      p = new_line((g == 0) ? L("CENTRE A ", "CENTER A ") : L("CENTRE B ", "CENTER B "));
      for(j = 0; j < 8; j++)
        p = put_x(p, shots[4][g][j], 2);
      (void)p;
      log_line();
    }
  /* the first bytes that are not zero in the last sample */
  for(j = 0; j < GUN_WINDOW && snap[j] == 0; j++)
    ;
  p = new_line(L("NON NUL ", "NONZERO "));
  p = put_u(p, j, 3);
  p = put_s(p, " ");
  for(k = 0; k < 12 && j + k < GUN_WINDOW; k++)
    p = put_x(p, snap[j + k], 2);
  (void)p;
  log_line();
}

/* ---------------------------------------------------------------- version 3.5 */

/* CARTE CPU: where in the field the program loses the processor (fast interrupts, other tasks such as the one that reads
   the control port). From a VBL wake-up, a loop reads the system clock without a break for one second; every reading that
   took more than CM_EXTRA us longer than the shortest one is a gap, kept with its place. The places are folded on the field
   period measured between two wake-ups and given in lines after the wake-up (63.556 us each). The page shows the time lost per
   field in windows of 16 lines, then the runs of lines where gaps come back (one empty line allowed inside a run), kept when
   they come in at least a quarter of the fields: first and last line, mean us per gap, number of gaps. */
#define CM_GAPS   1500
#define CM_SPAN   1000000
#define CM_EXTRA  40
#define CM_LINES  264
#define CM_WINS   17

static u32 cm_at[CM_GAPS];
static u16 cm_len[CM_GAPS];
static u16 cm_cnt[CM_LINES];
static u32 cm_tot[CM_LINES];

void group_cpumap(void)
{
  u32 win[CM_WINS];
  u32 base;
  u32 wake;
  u32 t0;
  u32 t1;
  u32 t;
  u32 prev;
  u32 d;
  u32 n;
  u32 jumps;
  u32 fields;
  u32 period;
  u32 line;
  u32 lost;
  u32 lt;
  u32 i;
  u32 a;
  u32 c;
  u32 s;
  u32 shown;
  char *p;

  if(need_dev() < 0)
    return;

  /* the line counter just after a wake-up, read as on disc 2.3 (14 on the FZ-10) */
  WaitVBL(b_vbl_ioreq, 1);
  wake = vi_line();

  /* the cost of one clock reading: the shortest of 64 */
  base = 0xFFFFFFFF;
  prev = usec_now();
  for(i = 0; i < 64; i++)
    {
      t = usec_now();
      if(t - prev < base)
        base = t - prev;
      prev = t;
    }

  progress(L("CARTE CPU", "CPU MAP  "));
  memset(cm_cnt, 0, sizeof(cm_cnt));
  memset(cm_tot, 0, sizeof(cm_tot));
  memset(win, 0, sizeof(win));
  WaitVBL(b_vbl_ioreq, 1);
  t0 = usec_now();
  prev = t0;
  lt = 0;
  n = 0;
  jumps = 0;
  do
    {
      t = usec_now();
      d = t - prev;
      /* a reading behind the last good one (the low counter wrapped before the high part) or more than 20 ms after it
         (seconds and microseconds not read together) is left out: the next one is measured from the last good one */
      if((s32)d < 0 || d > 20000)
        {
          jumps++;
          continue;
        }
      if(d > base + CM_EXTRA && n < CM_GAPS)
        {
          cm_at[n] = lt;
          cm_len[n] = (u16)(d - base);
          n++;
        }
      lt += d;
      prev = t;
    }
  while(lt < CM_SPAN && jumps < 100000);
  WaitVBL(b_vbl_ioreq, 1);
  t1 = usec_now();
  fields = (t1 - t0 + 8341) / 16683;
  period = (fields != 0) ? (t1 - t0) / fields : 0;
  if(period < 16500 || period > 16900)
    period = 16683;
  fields = lt / period;
  if(fields == 0)
    fields = 1;

  lost = 0;
  for(i = 0; i < n; i++)
    {
      line = (cm_at[i] % period) * 1000 / 63556;
      if(line >= CM_LINES)
        line = CM_LINES - 1;
      cm_cnt[line]++;
      cm_tot[line] += cm_len[i];
      win[line / 16] += cm_len[i];
      lost += cm_len[i];
    }

  p = new_line(L("CHAMPS ", "FIELDS "));
  p = put_u(p, fields, 0);
  p = put_s(p, L(" PERIODE ", " PERIOD  "));
  p = put_u(p, period, 0);
  p = put_s(p, L(" APPEL ", " CALL  "));
  p = put_u(p, base, 0);
  (void)p;
  log_line();
  p = new_line(L("ECARTS ", "GAPS   "));
  p = put_u(p, n, 0);
  p = put_s(p, L(" PERDU/CHAMP ", " LOST/FIELD  "));
  p = put_u(p, lost / fields, 0);
  p = put_s(p, L(" SAUTS ", " JUMPS "));
  p = put_u(p, jumps, 0);
  (void)p;
  log_line();
  p = new_line(L("REVEIL LIGNE ", "WAKE-UP LINE "));
  p = put_u(p, wake, 0);
  (void)p;
  log_line();
  text_line(L("US PERDUS/CHAMP PAR 16 LIGNES APRES REVEIL", "US LOST/FIELD BY 16 LINES AFTER WAKE-UP..."));
  for(i = 0; i < CM_WINS; i += 4)
    {
      p = new_line("");
      for(a = i; a < i + 4 && a < CM_WINS; a++)
        {
          p = put_u(p, a * 16, 3);
          p = put_u(p, win[a] / fields, 5);
          p = put_s(p, "  ");
        }
      (void)p;
      log_line();
    }
  text_line(L("GROUPES LIGNES      US   NB", "GROUPS  LINES       US   NB"));
  shown = 0;
  i = 0;
  while(i < CM_LINES && shown < 8)
    {
      if(cm_cnt[i] == 0)
        {
          i++;
          continue;
        }
      a = i;
      c = 0;
      s = 0;
      while(i < CM_LINES && (cm_cnt[i] != 0 || (i + 1 < CM_LINES && cm_cnt[i + 1] != 0)))
        {
          c += cm_cnt[i];
          s += cm_tot[i];
          i++;
        }
      if(c * 4 >= fields)
        {
          p = new_line("       ");
          p = put_u(p, a, 3);
          p = put_s(p, "-");
          p = put_u(p, i - 1, 3);
          p = put_u(p, s / c, 8);
          p = put_u(p, c, 5);
          (void)p;
          log_line();
          shown++;
        }
    }
}

/* PISTOLET BALAYAGE (3.5): the gun rests, aimed at the cross in the centre. Page 1: a white bar 4 pixels wide, the whole
   height, moves from x 96 to 222 by 2 pixels; page 2: a white bar 2 lines high, the whole width, moves from line 56 to 182
   by 2 lines. Each place is shown 6 fields and read in the last 4. For the places between the first and the last where the
   gun saw the bar: the place, the counter and the line count of the last reading that saw it (0 0: not seen there). */
#define GS_STEPS 64
void group_gunsweep(int page)
{
  static u32 gs_cnt[GS_STEPS];
  static u8 gs_hits[GS_STEPS];
  static u8 snap[GUN_WINDOW];
  Item bm;
  u32 k;
  u32 f;
  u32 a;
  u32 pos;
  u32 base;
  u32 n;
  u32 first;
  u32 last;
  u32 col;
  int at;
  char *p;

  if(need_dev() < 0)
    return;
  fill_white[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_white[2] = (u32)MakeRGB15(31, 31, 31) << 16;
  fill_black[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_black[2] = (u32)MakeRGB15(0, 0, 0) << 16;
  bm = b_sc.sc_BitmapItems[0];
  base = (page == 0) ? 96 : 56;

  /* the gun rests aimed at the cross; A on the pad starts */
  gun_rect(bm, fill_black, 0, 0, SCREEN_W, SCREEN_H);
  gun_rect(bm, fill_white, 152, 120, 17, 1);
  gun_rect(bm, fill_white, 160, 112, 1, 17);
  draw_row(0, 0, (page == 0) ? L("PISTOLET POSE, IMMOBILE, SUR LA CROIX", "GUN RESTING, STILL, ON THE CROSS     ") : L("PISTOLET TOUJOURS IMMOBILE", "GUN STILL IN PLACE        "), MakeRGB15(31, 31, 31));
  draw_row(0, 1, L("A: DEPART", "A: START "), MakeRGB15(31, 31, 31));
  DisplayScreen(b_sc.sc_Screens[0], 0);
  wait_buttons(ControlA | ControlStart);

  for(k = 0; k < GS_STEPS; k++)
    {
      pos = base + 2 * k;
      gun_rect(bm, fill_black, 0, 0, SCREEN_W, SCREEN_H);
      if(page == 0)
        gun_rect(bm, fill_white, (s32)pos, 0, 4, SCREEN_H);
      else
        gun_rect(bm, fill_white, 0, (s32)pos, SCREEN_W, 2);
      DisplayScreen(b_sc.sc_Screens[0], 0);
      gs_cnt[k] = 0;
      gs_hits[k] = 0;
      for(f = 0; f < 6; f++)
        {
          WaitVBL(b_vbl_ioreq, 1);
          if(f < 2)
            continue;
          a = pb_in_start();
          if(!pb_in_valid(a))
            continue;
          memcpy(snap, (const void *)a, GUN_WINDOW);
          at = gun_find(snap);
          if(at >= 0 && (snap[at + 4] & 31) != 0)
            {
              gs_cnt[k] = gun_counter(snap + at);
              gs_hits[k] = (u8)(snap[at + 4] & 31);
            }
        }
    }

  n = 0;
  first = 0;
  last = 0;
  for(k = 0; k < GS_STEPS; k++)
    if(gs_hits[k] != 0)
      {
        if(n == 0)
          first = k;
        last = k;
        n++;
      }
  p = new_line((page == 0) ? L("BARRE VERTICALE X ", "VERTICAL BAR X    ") : L("BARRE HORIZONTALE Y ", "HORIZONTAL BAR Y    "));
  if(n == 0)
    {
      p = put_s(p, L("JAMAIS VUE", "NEVER SEEN"));
      (void)p;
      log_line();
      return;
    }
  p = put_u(p, base + 2 * first, 0);
  p = put_s(p, "-");
  p = put_u(p, base + 2 * last, 0);
  p = put_s(p, L(" VUE ", " SEEN"));
  p = put_u(p, n, 0);
  p = put_s(p, L(" FOIS", " HITS"));
  (void)p;
  log_line();
  /* three places per line: place, counter, lines */
  col = 0;
  p = b_lines[0];
  for(k = first; k <= last; k++)
    {
      if(col == 0)
        p = new_line("");
      p = put_u(p, base + 2 * k, 4);
      p = put_u(p, gs_cnt[k], 7);
      p = put_u(p, gs_hits[k], 3);
      col++;
      if(col == 3)
        {
          log_line();
          col = 0;
        }
    }
  (void)p;
  if(col != 0)
    log_line();
}
