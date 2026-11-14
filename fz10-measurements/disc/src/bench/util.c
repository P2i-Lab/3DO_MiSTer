/*
  P2i Lab 3DO bench: time, text, result page, statistics, CRC32.
*/

#include "bench.h"

#include "debug.h"
#include "io.h"
#include "string.h"
#include "time.h"
#include "timerutils.h"

char b_lines[MAX_LINES][LINE_CHARS];
int b_line_count;

static u32 crc_table[256];
static int crc_ready;

/* ---------------------------------------------------------------- time */

/* One SampleSystemTimeTV per reading, as in version 2. Version 2.1 sampled twice and
   checked long spans against GetVBLTime; in Opera that froze the whole system during
   VITESSE twice, so it was taken out. A rare jump of about one second (seen once in
   Opera) is left to the median of the repeated measurements. */
u32 usec_now(void)
{
  TimeVal tv;

  SampleSystemTimeTV(&tv);
  return (u32)tv.tv_sec * 1000000 + (u32)tv.tv_usec;
}

u32 vbl_now(void)
{
  u32 hi;
  u32 lo;

  hi = 0;
  lo = 0;
  GetVBLTime(b_timer_ioreq, &hi, &lo);
  return lo;
}

void clock_start(Clock *c)
{
  c->vbl = 0;
  c->us  = usec_now();
}

u32 clock_us(const Clock *c)
{
  return usec_now() - c->us;
}

/* ---------------------------------------------------------------- text */

char *put_s(char *p, const char *s)
{
  while(*s != 0)
    *p++ = *s++;
  *p = 0;
  return p;
}

/* v in decimal, right-aligned in width characters */
char *put_u(char *p, u32 v, int width)
{
  char tmp[12];
  int n;

  n = 0;
  do
    {
      tmp[n++] = (char)('0' + (int)(v % 10));
      v /= 10;
    }
  while(v != 0);
  while(width > n)
    {
      *p++ = ' ';
      width--;
    }
  while(n > 0)
    *p++ = tmp[--n];
  *p = 0;
  return p;
}

/* the low digits*4 bits of v in hexadecimal */
char *put_x(char *p, u32 v, int digits)
{
  int i;
  u32 d;

  for(i = digits - 1; i >= 0; i--)
    {
      d = (v >> (i * 4)) & 15;
      *p++ = (char)((d < 10) ? ('0' + d) : ('A' + d - 10));
    }
  *p = 0;
  return p;
}

char *new_line(const char *name)
{
  if(b_line_count >= MAX_LINES)
    b_line_count = MAX_LINES - 1;
  return put_s(b_lines[b_line_count++], name);
}

/* each result goes on screen at once: if a test hangs, the last line shown tells
   which one. No kprintf: nothing shows it (not Opera, not the console), and in
   version 2.2 a deep stack at that point overran the 8 KB stack, damaged the cel
   data and the next DrawCels never ended (found in an Opera memory dump). */
void log_line(void)
{
  draw_row(0, 1 + b_line_count, b_lines[b_line_count - 1], MakeRGB15(31, 31, 31));
}

void text_line(const char *s)
{
  new_line(s);
  log_line();
}

/* the name of the test about to run, on the status line of the shown page */
void progress(const char *name)
{
  char buf[LINE_CHARS];
  char *p;
  int n;

  p = put_s(buf, L("EN COURS: ", "RUNNING:  "));
  p = put_s(p, name);
  n = (int)(p - buf);
  while(n < 30)
    {
      *p++ = ' ';
      n++;
    }
  *p = 0;
  draw_row(0, 21, buf, MakeRGB15(31, 31, 0));
}

void clear_screen(int screen)
{
  SetVRAMPages(b_vram_ioreq, b_sc.sc_Bitmaps[screen]->bm_Buffer, 0,
               (s32)b_sc.sc_NumBitmapPages, 0xFFFFFFFF);
}

void draw_row(int screen, int row, const char *s, Color color)
{
  GrafCon gc;

  memset(&gc, 0, sizeof(gc));
  SetFGPen(&gc, color);
  SetBGPen(&gc, MakeRGB15(0, 0, 0));
  MoveTo(&gc, 8, 14 + row * 9);
  DrawText8(&gc, b_sc.sc_BitmapItems[screen], (const u8 *)s);
}

/* title, optional column heads, the lines so far, a status line; on screen 0 */
void show_page(const char *title, const char *head, const char *status)
{
  int i;

  clear_screen(0);
  draw_row(0, 0, title, MakeRGB15(31, 31, 0));
  if(head != NULL)
    draw_row(0, 1, head, MakeRGB15(18, 18, 18));
  for(i = 0; i < b_line_count; i++)
    draw_row(0, 2 + i, b_lines[i], MakeRGB15(31, 31, 31));
  draw_row(0, 21, status, MakeRGB15(31, 31, 0));
  DisplayScreen(b_sc.sc_Screens[0], 0);
}

/* ---------------------------------------------------------------- stats */

void stat_reset(Stat *s)
{
  s->n = 0;
  s->min = 0xFFFFFFFF;
  s->max = 0;
}

void stat_add(Stat *s, u32 us)
{
  if(s->n < STAT_MAX)
    s->v[s->n++] = us;
  if(us < s->min)
    s->min = us;
  if(us > s->max)
    s->max = us;
}

/* kept off the stack: version 2.1 stalled in Opera once Stat grew to 64 samples */
static u32 median_tmp[STAT_MAX];

u32 stat_median(const Stat *s)
{
  u32 i;
  u32 j;
  u32 x;

  if(s->n == 0)
    return 0;
  for(i = 0; i < s->n; i++)
    {
      x = s->v[i];
      for(j = i; j > 0 && median_tmp[j - 1] > x; j--)
        median_tmp[j] = median_tmp[j - 1];
      median_tmp[j] = x;
    }
  return median_tmp[s->n / 2];
}

/* "NAME        MIN    MED    MAX" in microseconds; the median ignores a stray sample */
void stat_line(const char *name, const Stat *s)
{
  char *p;

  p = new_line(name);
  p = put_u(p, (s->n != 0) ? s->min : 0, 7);
  p = put_u(p, stat_median(s), 7);
  p = put_u(p, s->max, 7);
  (void)p;
  log_line();
}

void value_line(const char *name, u32 us)
{
  char *p;

  p = new_line(name);
  p = put_u(p, us, 8);
  p = put_s(p, " US");
  (void)p;
  log_line();
}

/* ---------------------------------------------------------------- grids */

/* Rows of numbers without names, to fit more on a page: each row starts with a tag
   and the row number, and the meaning of each position is given by the order of the
   measurements in the source. */
static int grid_cols;
static int grid_used;
static int grid_row;
static char grid_tag;
static char *grid_p;

void grid_begin(char tag, int cols)
{
  grid_tag  = tag;
  grid_cols = cols;
  grid_used = 0;
  grid_row  = 0;
  grid_p    = NULL;
}

static void grid_open(void)
{
  char name[3];

  name[0] = grid_tag;
  name[1] = "0123456789ABCDEFGHIJ"[grid_row % 20];
  name[2] = 0;
  grid_row++;
  grid_p = new_line(name);
}

static void grid_step(void)
{
  grid_used++;
  if(grid_used == grid_cols)
    {
      log_line();
      grid_used = 0;
    }
}

void grid_u(u32 v)
{
  if(grid_used == 0)
    grid_open();
  grid_p = put_s(grid_p, " ");
  grid_p = put_u(grid_p, v, 6);
  grid_step();
}

void grid_x(u32 v, int digits)
{
  if(grid_used == 0)
    grid_open();
  grid_p = put_s(grid_p, " ");
  grid_p = put_x(grid_p, v, digits);
  grid_step();
}

void grid_end(void)
{
  if(grid_used != 0)
    log_line();
  grid_used = 0;
}

/* ---------------------------------------------------------------- version 3.6: synchronised measures */

/* With b_sync set, every timed draw of the cel pages starts right after a VBL (WaitVBL), so the nine draws of
   one measure all start at the same point of the field. Drawn back to back, a draw lasting about half a field
   met the start of the next field one time in two, and the median of the nine fell on one of two values up to
   10 % apart (disc 3.5 results, 2026-11-13). b_spread_max is the largest max - min of the draws of one measure
   on the page, b_spread_n the number of measures spread over SPREAD_LIM us, b_jumps the number of measures with
   a clock jump (over 50 ms between min and max). */
#define SPREAD_LIM  64
u32 b_sync;
u32 b_spread_max;
u32 b_spread_n;
u32 b_jumps;

void sync_reset(void)
{
  b_spread_max = 0;
  b_spread_n   = 0;
  b_jumps      = 0;
}

void sync_note(const Stat *s)
{
  u32 d;

  if(s->n == 0)
    return;
  d = s->max - s->min;
  if(d > 50000)
    {
      b_jumps++;
      return;
    }
  if(d > b_spread_max)
    b_spread_max = d;
  if(d > SPREAD_LIM)
    b_spread_n++;
}

/* the line under the title of a synchronised page: "SYNC SPREAD 48 >64:0 JUMPS:0" */
void sync_head(char *buf)
{
  char *p;

  p = put_s(buf, L("SYNC ECART ", "SYNC SPREAD "));
  p = put_u(p, b_spread_max, 0);
  p = put_s(p, " >64:");
  p = put_u(p, b_spread_n, 0);
  p = put_s(p, L(" SAUTS:", " JUMPS:"));
  p = put_u(p, b_jumps, 0);
  (void)p;
}

/* ---------------------------------------------------------------- CRC32, random */

/* CRC-32 (reflected, polynomial EDB88320), as zlib: start with 0 */
u32 crc32_update(u32 crc, const u8 *p, u32 n)
{
  u32 c;
  u32 i;
  int k;

  if(!crc_ready)
    {
      for(i = 0; i < 256; i++)
        {
          c = i;
          for(k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
          crc_table[i] = c;
        }
      crc_ready = 1;
    }
  c = crc ^ 0xFFFFFFFF;
  while(n-- != 0)
    c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFF;
}

/* 32-bit linear congruential generator (Numerical Recipes) */
u32 rand_next(u32 *seed)
{
  *seed = *seed * 1664525 + 1013904223;
  return *seed;
}
