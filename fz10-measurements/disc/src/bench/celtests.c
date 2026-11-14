/*
  P2i Lab 3DO bench, groups CELS DETAIL (draw times of many cel kinds, to model
  the cel engine timing) and RENDU CRC (CRC32 of the frame buffer after drawing
  cels with less common features, to compare pixels exactly).
*/

#include "bench.h"

#include "mem.h"
#include "string.h"

#define REPS         30
#define LIST_CELS    100

/* variant CCBs */
#define V_VRAM       0     /* 16 bits, source in VRAM */
#define V_6U         1     /* 6 bits coded unpacked */
#define V_4          2     /* 4 bits coded */
#define V_1          3     /* 1 bit coded */
#define V_RPT        4     /* packed 6 bits, repeat packets */
#define V_TRSP       5     /* packed 6 bits, transparent packets */
#define V_HALFSIZE   6     /* 16 bits at half size */
#define V_DOUBLE     7     /* 160x120 of the 16-bit source at twice the size */
#define V_ROT        8     /* 16 bits turned 45 degrees */
#define V_OUT        9     /* 16 bits entirely off the screen */
#define V_8TRSP      10    /* 8 bits coded, half of the colours transparent */
#define V_FILLHALF   11    /* Strahl's fill, averaged with the frame buffer */
#define V_IMGHALF    12    /* Strahl's slide, averaged */
#define V_NARROW     13    /* 16 bits, 8x240 */
#define V_WIDE       14    /* 16 bits, 320x8 */
#define V_PERSP      15    /* 160x120 turned, scaled and skewed (HDDX/HDDY) */
#define V_FLIP       16    /* 16 bits mirrored */
#define V_PIXA       17    /* 16 bits, unusual pixel processor setting A */
#define V_PIXB       18    /* 16 bits, setting B with USEAV */
#define V_PMODE      19    /* 6 bits coded, P bit chooses between two PIXC halves */
#define V_FRAC       20    /* 16 bits at a fractional position, clipped */
#define V_LIST       21    /* first of LIST_CELS 32x32 cels */
#define V_COUNT      (V_LIST + LIST_CELS)

#define VRAM16_BYTES (SCREEN_W * SCREEN_H * 2)
#define SRC6_WORDS   60    /* 320 pixels x 6 bits */
#define SRC4_WORDS   40
#define SRC1_WORDS   10
#define RPT_WORDS    3
#define TRSP_WORDS   2
#define SRC6_BYTES   (SCREEN_H * SRC6_WORDS * 4)
#define SRC4_BYTES   (SCREEN_H * SRC4_WORDS * 4)
#define SRC1_BYTES   (SCREEN_H * SRC1_WORDS * 4)
#define RPT_BYTES    (SCREEN_H * RPT_WORDS * 4 + 16)
#define TRSP_BYTES   (SCREEN_H * TRSP_WORDS * 4 + 16)
#define PLUTT_BYTES  64
#define VCCB_BYTES   (V_COUNT * sizeof(CCB))

static CCB *v;
static u16 *vram16;
static u32 *src6;
static u32 *src4;
static u32 *src1;
static u32 *rpt;
static u32 *trsp;
static u16 *plut_t;

static void variants_free(void)
{
  if(v != NULL)      FreeMem(v, VCCB_BYTES);
  if(vram16 != NULL) FreeMem(vram16, VRAM16_BYTES);
  if(src6 != NULL)   FreeMem(src6, SRC6_BYTES);
  if(src4 != NULL)   FreeMem(src4, SRC4_BYTES);
  if(src1 != NULL)   FreeMem(src1, SRC1_BYTES);
  if(rpt != NULL)    FreeMem(rpt, RPT_BYTES);
  if(trsp != NULL)   FreeMem(trsp, TRSP_BYTES);
  if(plut_t != NULL) FreeMem(plut_t, PLUTT_BYTES);
  v = NULL;
  vram16 = NULL;
  src6 = NULL;
  src4 = NULL;
  src1 = NULL;
  rpt = NULL;
  trsp = NULL;
  plut_t = NULL;
}

/* coded unpacked source: rows of words_per_row words, pixel value from (x, y) */
static void fill_coded(u32 *buf, u32 words_per_row, u32 bpp, u32 mode)
{
  u32 x;
  u32 y;
  u32 pos;
  u32 val;

  for(y = 0; y < SCREEN_H; y++)
    {
      pos = y * words_per_row * 32;
      for(x = 0; x < SCREEN_W; x++)
        {
          if(mode == 1)        /* 6 bits with the top bit set on every other 32-pixel band */
            val = (1 + ((x >> 3) + (y >> 3)) % 31) | (((x >> 5) & 1) << 5);
          else
            val = ((x >> 3) + (y >> 2)) & ((1u << bpp) - 1);
          put_bits(buf, &pos, val, bpp);
        }
    }
}

static int variants_setup(void)
{
  u32 i;
  u32 y;
  u32 pos;
  u32 n;
  u32 flags16;
  u32 flagspk;
  u32 pre0_16;
  u32 pre1_16;
  CCB *c;

  v      = (CCB *)AllocMem(VCCB_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  vram16 = (u16 *)AllocMem(VRAM16_BYTES, MEMTYPE_VRAM | MEMTYPE_CEL);
  src6   = (u32 *)AllocMem(SRC6_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  src4   = (u32 *)AllocMem(SRC4_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  src1   = (u32 *)AllocMem(SRC1_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  rpt    = (u32 *)AllocMem(RPT_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  trsp   = (u32 *)AllocMem(TRSP_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  plut_t = (u16 *)AllocMem(PLUTT_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  if(v == NULL || vram16 == NULL || src6 == NULL || src4 == NULL || src1 == NULL ||
     rpt == NULL || trsp == NULL || plut_t == NULL)
    {
      variants_free();
      return -1;
    }

  memcpy(vram16, b_src16, VRAM16_BYTES);
  fill_coded(src6, SRC6_WORDS, 6, 1);
  fill_coded(src4, SRC4_WORDS, 4, 0);
  fill_coded(src1, SRC1_WORDS, 1, 0);
  for(i = 0; i < 32; i++)
    plut_t[i] = (i & 1) ? 0 : b_plut[i];

  /* packed rows: 5 repeat packets of 64 pixels, or 5 transparent packets */
  for(y = 0; y < SCREEN_H; y++)
    {
      pos = y * RPT_WORDS * 32;
      put_bits(rpt, &pos, RPT_WORDS - 2, 8);
      for(n = 0; n < 5; n++)
        {
          put_bits(rpt, &pos, PACK_PACKED, 2);
          put_bits(rpt, &pos, 63, 6);
          put_bits(rpt, &pos, 1 + ((n + y / 8) % 31), 6);
        }
      put_bits(rpt, &pos, PACK_EOL, 2);

      pos = y * TRSP_WORDS * 32;
      put_bits(trsp, &pos, TRSP_WORDS - 2, 8);
      for(n = 0; n < 5; n++)
        {
          put_bits(trsp, &pos, PACK_TRANSPARENT, 2);
          put_bits(trsp, &pos, 63, 6);
        }
      put_bits(trsp, &pos, PACK_EOL, 2);
    }

  flags16 = b_ccbs[C_16BPP].ccb_Flags;
  flagspk = b_ccbs[C_IMAGE].ccb_Flags;
  pre0_16 = b_ccbs[C_16BPP].ccb_PRE0;
  pre1_16 = b_ccbs[C_16BPP].ccb_PRE1;

  init_ccb(&v[V_VRAM], flags16, vram16, NULL, pre0_16, pre1_16, PIXC_SOLID);

  init_ccb(&v[V_6U], flags16 | CCB_LDPLUT, src6, b_plut,
           PRE0_BPP_6 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SRC6_WORDS - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  init_ccb(&v[V_4], flags16 | CCB_LDPLUT, src4, b_plut,
           PRE0_BPP_4 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SRC4_WORDS - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  init_ccb(&v[V_1], flags16 | CCB_LDPLUT, src1, b_plut,
           PRE0_BPP_1 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SRC1_WORDS - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);

  init_ccb(&v[V_RPT], flagspk, rpt, b_plut, b_ccbs[C_IMAGE].ccb_PRE0, 0, PIXC_SOLID);
  init_ccb(&v[V_TRSP], flagspk, trsp, b_plut, b_ccbs[C_IMAGE].ccb_PRE0, 0, PIXC_SOLID);

  v[V_HALFSIZE] = b_ccbs[C_16BPP];
  v[V_HALFSIZE].ccb_HDX = 1 << 19;
  v[V_HALFSIZE].ccb_VDY = 1 << 15;

  /* the top-left 160x120 of the 16-bit source: rows still 320 pixels apart */
  init_ccb(&v[V_DOUBLE], flags16, b_src16, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((120 - 1) << PRE0_VCNT_SHIFT),
           ((SCREEN_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (160 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  v[V_DOUBLE].ccb_HDX = 2 << 20;
  v[V_DOUBLE].ccb_VDY = 2 << 16;

  /* 45 degrees: pixels go down-right, rows go down-left; 0.70711 */
  v[V_ROT] = b_ccbs[C_16BPP];
  v[V_ROT].ccb_HDX  = 741455;      /* 0.70711 in 12.20 */
  v[V_ROT].ccb_HDY  = 741455;
  v[V_ROT].ccb_VDX  = -46341;      /* in 16.16 */
  v[V_ROT].ccb_VDY  = 46341;
  v[V_ROT].ccb_XPos = 160 << 16;
  v[V_ROT].ccb_YPos = -(110 << 16);

  v[V_OUT] = b_ccbs[C_16BPP];
  v[V_OUT].ccb_XPos = 400 << 16;

  v[V_8TRSP] = b_ccbs[C_8BPP];
  v[V_8TRSP].ccb_Flags &= ~(CCB_BGND | CCB_NOBLK);
  v[V_8TRSP].ccb_PLUTPtr = plut_t;

  v[V_FILLHALF] = b_ccbs[C_FILL];
  v[V_FILLHALF].ccb_PIXC = PIXC_HALF;
  v[V_IMGHALF] = b_ccbs[C_IMAGE];
  v[V_IMGHALF].ccb_PIXC = PIXC_HALF;

  init_ccb(&v[V_NARROW], flags16, b_src16, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SCREEN_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (8 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  init_ccb(&v[V_WIDE], flags16, b_src16, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((8 - 1) << PRE0_VCNT_SHIFT), pre1_16, PIXC_SOLID);

  /* 160x120 at 1.3 times, turned 30 degrees, with a small per-row change of HDX/HDY */
  v[V_PERSP] = v[V_DOUBLE];
  v[V_PERSP].ccb_HDX  = 1180482;   /* 1.3 cos 30 in 12.20 */
  v[V_PERSP].ccb_HDY  = 681574;    /* 1.3 sin 30 */
  v[V_PERSP].ccb_VDX  = -42598;    /* -1.3 sin 30 in 16.16 */
  v[V_PERSP].ccb_VDY  = 73782;     /* 1.3 cos 30 */
  v[V_PERSP].ccb_HDDX = 2048;      /* 12.20 */
  v[V_PERSP].ccb_HDDY = -1024;
  v[V_PERSP].ccb_XPos = 100 << 16;
  v[V_PERSP].ccb_YPos = 10 << 16;

  v[V_FLIP] = b_ccbs[C_16BPP];
  v[V_FLIP].ccb_HDX  = -(1 << 20);
  v[V_FLIP].ccb_XPos = 319 << 16;

  v[V_PIXA] = b_ccbs[C_16BPP];
  v[V_PIXA].ccb_PIXC = 0xCFC1CFC1;
  v[V_PIXB] = b_ccbs[C_16BPP];
  v[V_PIXB].ccb_Flags |= CCB_USEAV;
  v[V_PIXB].ccb_PIXC = 0x1CBE1CBE;

  v[V_PMODE] = v[V_6U];
  v[V_PMODE].ccb_PIXC = 0x1F811F00;

  v[V_FRAC] = b_ccbs[C_16BPP];
  v[V_FRAC].ccb_XPos = -((37 << 16) + 0x8000);
  v[V_FRAC].ccb_YPos = -((21 << 16) + 0x4000);

  for(i = 0; i < LIST_CELS; i++)
    {
      c = &v[V_LIST + i];
      init_ccb(c, flags16 & ~CCB_LAST, b_src16, NULL,
               PRE0_LINEAR | PRE0_BPP_16 | ((32 - 1) << PRE0_VCNT_SHIFT),
               ((SCREEN_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
               PRE1_TLLSB_PDC0 | (32 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
      c->ccb_XPos = (Coord)(((i * 29) % (SCREEN_W - 32)) << 16);
      c->ccb_YPos = (Coord)(((i * 17) % (SCREEN_H - 32)) << 16);
      if(i + 1 < LIST_CELS)
        c->ccb_NextPtr = &v[V_LIST + i + 1];
      else
        c->ccb_Flags |= CCB_LAST;
    }

  return 0;
}

void group_cels(void)
{
  if(variants_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  time_cels("16B VRAM  ", &v[V_VRAM], NULL, REPS);
  time_cels("6BPP NONPK", &v[V_6U], NULL, REPS);
  time_cels("4BPP      ", &v[V_4], NULL, REPS);
  time_cels("1BPP      ", &v[V_1], NULL, REPS);
  time_cels(L("PACK REPET", "PACK REPT "), &v[V_RPT], NULL, REPS);
  time_cels("PACK TRANS", &v[V_TRSP], NULL, REPS);
  time_cels("16B 1/2   ", &v[V_HALFSIZE], NULL, REPS);
  time_cels("16B X2    ", &v[V_DOUBLE], NULL, REPS);
  time_cels("16B ROT45 ", &v[V_ROT], NULL, REPS);
  time_cels(L("16B HORS  ", "16B OFF   "), &v[V_OUT], NULL, REPS);
  time_cels("8BPP TRANS", &v[V_8TRSP], NULL, REPS);
  time_cels(L("FOND DEMI ", "BKGND HALF"), &v[V_FILLHALF], NULL, REPS);
  time_cels(L("IMAGE DEMI", "IMAGE HALF"), &v[V_IMGHALF], NULL, REPS);
  time_cels("16B 8X240 ", &v[V_NARROW], NULL, REPS);
  time_cels("16B 320X8 ", &v[V_WIDE], NULL, REPS);
  time_cels("32X32 X100", &v[V_LIST], NULL, 10);
  variants_free();
}

/* the 16-bit picture over the whole hidden screen, then cel (if any); CRC32 of the screen */
static u32 render(CCB *cel)
{
  Bitmap *bm;

  bm = b_sc.sc_Bitmaps[1];
  DrawCels(b_sc.sc_BitmapItems[1], &b_ccbs[C_16BPP]);
  if(cel != NULL)
    DrawCels(b_sc.sc_BitmapItems[1], cel);
  return crc32_update(0, (const u8 *)bm->bm_Buffer, b_sc.sc_NumBitmapBytes);
}

void group_render(void)
{
  CCB *cases[24];
  char *p;
  int i;
  u32 crc;

  if(variants_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  cases[0]  = NULL;
  cases[1]  = &b_ccbs[C_IMAGE];
  cases[2]  = &b_ccbs[C_8BPP];
  cases[3]  = &b_ccbs[C_FILL];
  cases[4]  = &b_ccbs[C_HALF];
  cases[5]  = &v[V_6U];
  cases[6]  = &v[V_4];
  cases[7]  = &v[V_1];
  cases[8]  = &v[V_RPT];
  cases[9]  = &v[V_TRSP];
  cases[10] = &v[V_HALFSIZE];
  cases[11] = &v[V_DOUBLE];
  cases[12] = &v[V_ROT];
  cases[13] = &v[V_8TRSP];
  cases[14] = &v[V_LIST];
  cases[15] = &v[V_FILLHALF];
  cases[16] = &v[V_IMGHALF];
  cases[17] = &b_ccbs[C_SMALL];
  cases[18] = &v[V_PERSP];
  cases[19] = &v[V_FLIP];
  cases[20] = &v[V_PIXA];
  cases[21] = &v[V_PIXB];
  cases[22] = &v[V_PMODE];
  cases[23] = &v[V_FRAC];

  p = NULL;
  for(i = 0; i < 24; i++)
    {
      char name[8];
      char *q;

      q = put_s(name, "R");
      q = put_u(q, (u32)(i + 1), 0);
      (void)q;
      progress(name);
      crc = render(cases[i]);
      if((i & 1) == 0)
        p = new_line("R");
      else
        p = put_s(p, "  R");
      if(i + 1 < 10)
        p = put_s(p, "0");
      p = put_u(p, (u32)(i + 1), 0);
      p = put_s(p, " ");
      p = put_x(p, crc, 8);
      if(i & 1)
        log_line();
    }
  variants_free();
}
