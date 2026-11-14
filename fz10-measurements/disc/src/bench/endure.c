/*
  P2i Lab 3DO bench 3.1, group ENDURANCE: for five minutes, the same work again and again,
  each result checked against the first one: copies in DRAM and VRAM, VRAM page fills and
  copies, a frame of cels, blocks read from the CD. On a real console every counter of
  errors stays at zero; on an FPGA build whose timing is marginal, they may not. X stops.
*/

#include "bench.h"

#include "controlpad.h"
#include "event.h"
#include "mem.h"
#include "string.h"

#define EN_BYTES    32768
#define EN_PAGES    8
#define EN_SECONDS  300
#define EN_KINDS    6

static const char *en_names[EN_KINDS] =
  { "DRAM  ", "VRAM  ", L("REMPL ", "FILL  "), L("COPIE ", "COPY  "), "CELS  ", "CD    " };

static u32 en_err[EN_KINDS];

static void en_fill(u32 *p, u32 words, u32 seed)
{
  u32 i;

  for(i = 0; i < words; i++)
    p[i] = rand_next(&seed);
}

static void en_report(u32 iter, u32 seconds)
{
  char *p;
  u32 i;

  b_line_count = 0;
  p = new_line(L("TOURS ", "LAPS  "));
  p = put_u(p, iter, 7);
  p = put_s(p, L("  TEMPS ", "  TIME  "));
  p = put_u(p, seconds, 4);
  p = put_s(p, " S");
  (void)p;
  log_line();
  for(i = 0; i < EN_KINDS; i++)
    {
      p = new_line(en_names[i]);
      p = put_s(p, L("ERREURS ", "ERRORS  "));
      p = put_u(p, en_err[i], 7);
      (void)p;
      log_line();
    }
}

void group_endurance(void)
{
  u32 *a;
  u32 *b;
  u8 *vr;
  u8 *vr2;
  u32 *w;
  Item bm;
  u32 page;
  u32 span;
  u32 ref;
  u32 cel_ref;
  u32 h;
  u32 i;
  u32 iter;
  u32 t0;
  u32 elapsed;
  u32 seed;
  u32 buttons;
  int cd_ok;
  int r;

  a = (u32 *)AllocMem(EN_BYTES, MEMTYPE_DRAM);
  b = (u32 *)AllocMem(EN_BYTES, MEMTYPE_DRAM);
  if(a == NULL || b == NULL)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      if(a != NULL) FreeMem(a, EN_BYTES);
      if(b != NULL) FreeMem(b, EN_BYTES);
      return;
    }
  for(i = 0; i < EN_KINDS; i++)
    en_err[i] = 0;
  vr   = (u8 *)b_sc.sc_Bitmaps[1]->bm_Buffer;
  bm   = b_sc.sc_BitmapItems[1];
  page = (u32)GetPageSize(MEMTYPE_VRAM);
  span = EN_PAGES * page;
  vr2  = vr + 16 * page;
  en_fill(a, EN_BYTES / 4, 0x1234567u);
  ref = crc32_update(0, (const u8 *)a, EN_BYTES);
  cd_ok = (cd_stress_open() >= 0);
  seed = 0x51ED270Bu;
  cel_ref = 0;
  iter = 0;
  t0 = usec_now();
  draw_row(0, 21, L("X: ARRETER", "X: STOP   "), MakeRGB15(31, 31, 0));
  for(;;)
    {
      /* a frame of cels: drawn the same way each time */
      DrawCels(bm, &b_ccbs[C_FILL]);
      DrawCels(bm, &b_ccbs[C_SMALL]);
      h = crc32_update(0, vr, SCREEN_W * SCREEN_H * 2);
      if(iter == 0)
        cel_ref = h;
      else if(h != cel_ref)
        en_err[4]++;

      memset(b, 0, EN_BYTES);
      memcpy(b, a, EN_BYTES);
      if(crc32_update(0, (const u8 *)b, EN_BYTES) != ref)
        en_err[0]++;

      memcpy(vr, a, EN_BYTES);
      if(crc32_update(0, vr, EN_BYTES) != ref)
        en_err[1]++;

      SetVRAMPages(b_vram_ioreq, vr2, 0x5A5AA5A5, EN_PAGES, 0xFFFFFFFF);
      w = (u32 *)vr2;
      for(i = 0; i < span / 4; i++)
        if(w[i] != 0x5A5AA5A5)
          {
            en_err[2]++;
            break;
          }

      CopyVRAMPages(b_vram_ioreq, vr2, vr, EN_PAGES, 0xFFFFFFFF);
      if(crc32_update(0, vr, span) != crc32_update(0, vr2, span))
        en_err[3]++;

      if(cd_ok)
        {
          r = cd_stress_read(4000 + rand_next(&seed) % 60000);
          if(r != 0)
            en_err[5]++;
        }

      iter++;
      elapsed = usec_now() - t0;
      if((iter & 15) == 0)
        en_report(iter, elapsed / 1000000);
      buttons = 0;
      DoControlPad(1, &buttons, 0);
      if((buttons & ControlX) != 0 || elapsed >= EN_SECONDS * 1000000u)
        break;
    }
  en_report(iter, elapsed / 1000000);
  if(cd_ok)
    cd_stress_close();
  FreeMem(a, EN_BYTES);
  FreeMem(b, EN_BYTES);
}
