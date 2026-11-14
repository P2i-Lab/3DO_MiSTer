/*
  P2i Lab 3DO bench 3.0, group MIRE VIDEO: a test picture to photograph, with the video
  interpolation switched by A. Colour ramps, one-pixel checks and lines, and a frame on
  the outermost pixels of the picture.
*/

#include "bench.h"

#include "controlpad.h"
#include "event.h"
#include "mem.h"
#include "string.h"

static void plot(u16 *m, u32 x, u32 y, u32 rgb)
{
  m[y * SCREEN_W + x] = (u16)rgb;
}

static void fill_mire(u16 *m)
{
  u32 x;
  u32 y;
  u32 v;

  for(y = 0; y < SCREEN_H; y++)
    for(x = 0; x < SCREEN_W; x++)
      {
        v = 0;
        if(y < 80)
          {
            /* red, green, blue and grey ramps, 32 steps of 10 pixels */
            u32 s;

            s = x / 10;
            if(y < 20)
              v = MakeRGB15(s, 0, 0);
            else if(y < 40)
              v = MakeRGB15(0, s, 0);
            else if(y < 60)
              v = MakeRGB15(0, 0, s);
            else
              v = MakeRGB15(s, s, s);
          }
        else if(y < 120)
          v = ((x ^ y) & 1) ? MakeRGB15(31, 31, 31) : 0;    /* one-pixel check */
        else if(y < 160)
          v = (x & 1) ? MakeRGB15(31, 31, 31) : 0;          /* vertical lines */
        else if(y < 200)
          v = (y & 1) ? MakeRGB15(31, 31, 31) : 0;          /* horizontal lines */
        else
          v = MakeRGB15((x >> 3) & 31, 31 - ((x >> 3) & 31), (y - 200) * 3 / 4);
        plot(m, x, y, v);
      }
  for(x = 0; x < SCREEN_W; x++)
    {
      plot(m, x, 0, MakeRGB15(31, 31, 0));
      plot(m, x, SCREEN_H - 1, MakeRGB15(31, 31, 0));
    }
  for(y = 0; y < SCREEN_H; y++)
    {
      plot(m, 0, y, MakeRGB15(0, 31, 31));
      plot(m, SCREEN_W - 1, y, MakeRGB15(0, 31, 31));
    }
}

void group_mire(void)
{
  u16 *m;
  CCB c;
  u32 buttons;
  u32 mode;
  Item scr;

  m = (u16 *)AllocMem(SCREEN_W * SCREEN_H * 2, MEMTYPE_DRAM | MEMTYPE_CEL);
  if(m == NULL)
    return;
  fill_mire(m);
  c = b_ccbs[C_16BPP];
  c.ccb_SourcePtr = (CelData *)m;
  DrawCels(b_sc.sc_BitmapItems[0], &c);
  DisplayScreen(b_sc.sc_Screens[0], 0);

  scr = b_sc.sc_Screens[0];
  mode = 0;
  for(;;)
    {
      /* mode 0: as set by the system; then both off, horizontal only, vertical only, both on */
      buttons = wait_buttons(ControlA | ControlStart | ControlX);
      if(buttons & ControlX)
        break;
      mode = (mode + 1) % 4;
      if(mode & 1) EnableHAVG(scr); else DisableHAVG(scr);
      if(mode & 2) EnableVAVG(scr); else DisableVAVG(scr);
      DisplayScreen(scr, 0);
    }
  EnableHAVG(scr);
  EnableVAVG(scr);
  FreeMem(m, SCREEN_W * SCREEN_H * 2);
}
