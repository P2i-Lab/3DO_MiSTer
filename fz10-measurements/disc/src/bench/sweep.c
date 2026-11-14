/*
  P2i Lab 3DO bench 3.0, group CELS BALAYAGE: draw times of cels varied one parameter
  at a time, to separate what the cel engine pays per cel, per row, per source word
  and per pixel. Each number is the median time, in microseconds, of drawing k_for()
  identical cels. The numbers appear in the order of the calls below.
*/

#include "bench.h"

#include "graphics.h"
#include "mem.h"
#include "string.h"

#define REPS       9
#define LIST_MAX   128
#define GEN_BYTES  (176 * 1024)
#define MINI_WORDS 16
#define FULL_LD    (CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP)

static CCB *cc;
static u32 *gen;
static u32 *mini;
static u16 *plut_tr;
static u32 flags16;
static u32 flagspk;
static Item target;
static Stat st;

static const u16 ws[12]  = { 1, 2, 3, 4, 8, 16, 32, 64, 128, 160, 240, 320 };
static const u16 hs[10]  = { 1, 2, 3, 4, 8, 16, 32, 64, 120, 240 };
static const u16 ows[9]  = { 1, 2, 4, 8, 16, 32, 64, 128, 320 };
static const u16 ohs[4]  = { 1, 8, 60, 240 };
static const u8  pks[7]  = { 1, 2, 4, 8, 16, 32, 64 };
static const u16 scales[8] = { 64, 128, 192, 320, 384, 512, 768, 1024 };   /* 1/256 units */
static const s16 clips[12][2] =
  {
    { -160, 0 }, { 160, 0 }, { 0, -120 }, { 0, 120 }, { 320, 0 }, { -320, 0 },
    { 400, 0 }, { -400, 0 }, { 0, 240 }, { 0, -240 }, { 0, 300 }, { 0, -300 }
  };
static const s16 sclips[6][2] =
  {
    { 160, 0 }, { 400, 0 }, { -400, 0 }, { 0, 120 }, { 0, 300 }, { 0, -300 }
  };
/* cos, sin in 16.16 for 15, 30, 60, 90, 180 degrees */
static const s32 rots[5][2] =
  {
    { 63302, 16962 }, { 56756, 32768 }, { 32768, 56756 }, { 0, 65536 }, { -65536, 0 }
  };
static const u16 pixcs[8] =
  { 0x1F00, 0x1F40, 0x1F80, 0x1F81, 0x1FC0, 0x9F00, 0x9F80, 0x0F00 };

static void sw_free(void)
{
  if(cc != NULL)      FreeMem(cc, LIST_MAX * sizeof(CCB));
  if(gen != NULL)     FreeMem(gen, GEN_BYTES);
  if(mini != NULL)    FreeMem(mini, MINI_WORDS * 4);
  if(plut_tr != NULL) FreeMem(plut_tr, 64);
  cc = NULL;
  gen = NULL;
  mini = NULL;
  plut_tr = NULL;
}

static int sw_setup(void)
{
  u32 i;

  cc      = (CCB *)AllocMem(LIST_MAX * sizeof(CCB), MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  gen     = (u32 *)AllocMem(GEN_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  mini    = (u32 *)AllocMem(MINI_WORDS * 4, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  plut_tr = (u16 *)AllocMem(64, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  if(cc == NULL || gen == NULL || mini == NULL || plut_tr == NULL)
    {
      sw_free();
      return -1;
    }
  for(i = 1; i < 32; i++)
    plut_tr[i] = (u16)MakeRGB15(i, 31 - i, 12);
  plut_tr[0] = 0;
  flags16 = b_ccbs[C_16BPP].ccb_Flags;
  flagspk = b_ccbs[C_IMAGE].ccb_Flags;
  target  = b_sc.sc_BitmapItems[1];
  return 0;
}

/* how many identical cels make one measure */
static u32 k_for(u32 w, u32 h)
{
  u32 area;

  area = w * h;
  if(area < 64)
    return 128;
  if(area < 1024)
    return 32;
  if(area < 8192)
    return 8;
  return 1;
}

static void make16(CCB *c, u32 w, u32 h, s32 x, s32 y)
{
  init_ccb(c, flags16, b_src16, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((h - 1) << PRE0_VCNT_SHIFT),
           ((SCREEN_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (w - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  c->ccb_XPos   = (Coord)(x * 65536);
  c->ccb_YPos   = (Coord)(y * 65536);
  c->ccb_Width  = (s32)w;
  c->ccb_Height = (s32)h;
}

static void set_list(const CCB *c, u32 k)
{
  u32 i;

  for(i = 0; i < k; i++)
    {
      cc[i] = *c;
      cc[i].ccb_Flags  &= ~CCB_LAST;
      cc[i].ccb_Flags  |= CCB_NPABS;      /* version 3.2: the fill cel had a relative pointer here */
      cc[i].ccb_NextPtr = &cc[i + 1];
    }
  cc[k - 1].ccb_Flags  |= CCB_LAST;
  cc[k - 1].ccb_NextPtr = NULL;
}

static void measure(void)
{
  u32 i;
  u32 t0;

  stat_reset(&st);
  for(i = 0; i < REPS; i++)
    {
      if(b_sync)
        WaitVBL(b_vbl_ioreq, 1);   /* version 3.6: every draw starts at the same point of the field */
      t0 = usec_now();
      DrawCels(target, cc);
      stat_add(&st, usec_now() - t0);
    }
  sync_note(&st);
  grid_u(stat_median(&st));
}

static void tk(const CCB *c, u32 k)
{
  set_list(c, k);
  measure();
}

/* the same, every cel of the list but the first without the flags in clear */
static void tk_light(const CCB *c, u32 k, u32 clear)
{
  u32 i;

  set_list(c, k);
  for(i = 1; i < k; i++)
    cc[i].ccb_Flags &= ~clear;
  measure();
}

/* coded or uncoded unpacked source in gen: 8 different rows repeated */
static void make_unpacked(CCB *c, u32 code, u32 bits, u32 linear)
{
  u32 x;
  u32 y;
  u32 pos;
  u32 wpr;
  u32 mask;

  wpr  = (SCREEN_W * bits + 31) / 32;
  mask = (bits >= 32) ? 0xFFFFFFFF : ((1u << bits) - 1);
  memset(gen, 0, wpr * SCREEN_H * 4);
  for(y = 0; y < 8; y++)
    {
      pos = y * wpr * 32;
      for(x = 0; x < SCREEN_W; x++)
        put_bits(gen, &pos, (((x >> 2) + y * 3) & mask), bits);
    }
  for(y = 8; y < SCREEN_H; y++)
    memcpy(gen + y * wpr, gen + (y & 7) * wpr, wpr * 4);
  init_ccb(c, flags16 | CCB_LDPLUT, gen, b_plut,
           code | linear | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((wpr - PRE1_WOFFSET_PREFETCH) << ((bits >= 8) ? PRE1_WOFFSET10_SHIFT : PRE1_WOFFSET8_SHIFT)) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
}

/* packed 6-bit rows of packets of n pixels, every row the same */
static void make_packed(CCB *c, u32 type, u32 n)
{
  u32 x;
  u32 m;
  u32 k;
  u32 y;
  u32 pos;
  u32 words;

  pos = 0;
  memset(gen, 0, 1024);
  put_bits(gen, &pos, 0, 8);
  for(x = 0; x < SCREEN_W; x += m)
    {
      m = SCREEN_W - x;
      if(m > n)
        m = n;
      put_bits(gen, &pos, type, 2);
      put_bits(gen, &pos, m - 1, 6);
      if(type == PACK_PACKED)
        put_bits(gen, &pos, 1 + (x >> 3) % 31, 6);
      else if(type == PACK_LITERAL)
        for(k = 0; k < m; k++)
          put_bits(gen, &pos, 1 + (x + k) % 31, 6);
    }
  put_bits(gen, &pos, PACK_EOL, 2);
  words = (pos + 31) >> 5;
  if(words < 2)
    words = 2;
  gen[0] |= (words - 2) << 24;
  for(y = 1; y < SCREEN_H; y++)
    memcpy(gen + y * words, gen, words * 4);
  init_ccb(c, flagspk, gen, b_plut, b_ccbs[C_IMAGE].ccb_PRE0, 0, PIXC_SOLID);
}

/* 6-bit unpacked: mode 0..3 = 0, 25, 50, 100 percent of transparent pixels at random,
   mode 4 = 50 percent in blocks of 8 */
static void make_trans(CCB *c, u32 mode)
{
  u32 x;
  u32 y;
  u32 pos;
  u32 v;
  u32 r;
  u32 seed;

  memset(gen, 0, 60 * SCREEN_H * 4);
  seed = 77 + mode;
  for(y = 0; y < 8; y++)
    {
      pos = y * 60 * 32;
      for(x = 0; x < SCREEN_W; x++)
        {
          r = rand_next(&seed) >> 24;
          v = 1 + (x % 31);
          if(mode == 1 && r < 64)   v = 0;
          if(mode == 2 && r < 128)  v = 0;
          if(mode == 3)             v = 0;
          if(mode == 4 && ((x >> 3) & 1) != 0) v = 0;
          put_bits(gen, &pos, v, 6);
        }
    }
  for(y = 8; y < SCREEN_H; y++)
    memcpy(gen + y * 60, gen + (y & 7) * 60, 60 * 4);
  init_ccb(c, (flags16 | CCB_LDPLUT) & ~(CCB_BGND | CCB_NOBLK), gen, plut_tr,
           PRE0_BPP_6 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((60 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
}

static void page0(void)
{
  CCB c;
  u32 i;
  u32 t;

  grid_begin('A', 5);

  /* the corner engines: every cel of the earlier pages had ACE, LCE and TWD off */
  progress(L("MOTEURS", "ENGINES"));
  make16(&c, 320, 240, 0, 0);
  tk(&c, 1);                                        /* 16 bits, no flag */
  c.ccb_Flags |= CCB_ACE;                tk(&c, 1); /* ACE */
  c.ccb_Flags |= CCB_LCE;                tk(&c, 1); /* ACE + LCE */
  c.ccb_Flags &= ~CCB_ACE;               tk(&c, 1); /* LCE */
  c.ccb_Flags &= ~CCB_LCE;
  c.ccb_Flags |= CCB_TWD;                tk(&c, 1); /* TWD */
  c.ccb_Flags &= ~CCB_TWD;
  c.ccb_Flags |= CCB_MARIA;              tk(&c, 1); /* MARIA */
  c = b_ccbs[C_IMAGE];
  tk(&c, 1);                                        /* packed 6 bits */
  c.ccb_Flags |= CCB_ACE;                tk(&c, 1);
  c.ccb_Flags |= CCB_LCE;                tk(&c, 1);
  c = b_ccbs[C_FILL];
  c.ccb_Flags |= CCB_ACE;                tk(&c, 1); /* fill, ACE */
  make16(&c, 32, 32, 0, 0);
  c.ccb_Flags |= CCB_ACE;                tk(&c, 8); /* 32 x 32 x 8, ACE */
  make16(&c, 320, 2, 0, 0);
  c.ccb_Flags |= CCB_ACE;                tk(&c, 8); /* 320 x 2 x 8, ACE */

  progress(L("LARGEURS", "WIDTHS  "));
  for(i = 0; i < 12; i++)
    {
      make16(&c, ws[i], SCREEN_H, 0, 0);
      tk(&c, k_for(ws[i], SCREEN_H));
    }
  progress(L("HAUTEURS", "HEIGHTS "));
  for(i = 0; i < 10; i++)
    {
      make16(&c, SCREEN_W, hs[i], 0, 0);
      tk(&c, k_for(SCREEN_W, hs[i]));
    }
  progress(L("HORS ECRAN", "OFF SCREEN"));
  for(i = 0; i < 9; i++)
    {
      make16(&c, ows[i], SCREEN_H, 400, 0);
      tk(&c, k_for(ows[i], SCREEN_H));
    }
  for(i = 0; i < 4; i++)
    {
      make16(&c, SCREEN_W, ohs[i], 400, 0);
      tk(&c, k_for(SCREEN_W, ohs[i]));
    }
  progress(L("PARITE", "PARITY"));
  for(i = 1; i <= 4; i++)
    {
      make16(&c, SCREEN_W, i, 0, 1);
      tk(&c, k_for(SCREEN_W, i));
    }
  for(i = 1; i <= 3; i++)
    {
      make16(&c, i, SCREEN_H, 1, 0);
      tk(&c, k_for(i, SCREEN_H));
    }

  progress(L("BITS PAR PIXEL", "BITS PER PIXEL"));
  make_unpacked(&c, PRE0_BPP_1, 1, 0);            tk(&c, 1);
  make_unpacked(&c, PRE0_BPP_2, 2, 0);            tk(&c, 1);
  make_unpacked(&c, PRE0_BPP_4, 4, 0);            tk(&c, 1);
  make_unpacked(&c, PRE0_BPP_6, 6, 0);            tk(&c, 1);
  make_unpacked(&c, PRE0_BPP_8, 8, 0);            tk(&c, 1);
  make_unpacked(&c, PRE0_BPP_8, 8, PRE0_LINEAR);  tk(&c, 1);
  make16(&c, SCREEN_W, SCREEN_H, 0, 0);
  c.ccb_PRE0 &= ~PRE0_LINEAR;
  c.ccb_Flags |= CCB_LDPLUT;
  c.ccb_PLUTPtr = b_plut;
  tk(&c, 1);                                      /* 16 bits coded */

  progress(L("PAQUETS", "PACKETS"));
  for(t = 0; t < 3; t++)
    for(i = 0; i < 7; i++)
      {
        make_packed(&c, (t == 0) ? PACK_LITERAL : ((t == 1) ? PACK_PACKED : PACK_TRANSPARENT), pks[i]);
        tk(&c, 1);
      }
  progress(L("TRANSPARENCE", "TRANSPARENCY"));
  for(i = 0; i < 5; i++)
    {
      make_trans(&c, i);
      tk(&c, 1);
    }
  grid_end();
}

static void ccb_costs(void)
{
  CCB c;
  CCB f;

  progress("CCB");
  make16(&c, 1, 1, 10, 10);
  tk(&c, 128);                              /* every load flag */
  tk_light(&c, 128, CCB_LDSIZE);
  tk_light(&c, 128, CCB_LDPRS);
  tk_light(&c, 128, CCB_LDPPMP);
  tk_light(&c, 128, FULL_LD);

  /* preamble in the data */
  mini[0] = c.ccb_PRE0;
  mini[1] = c.ccb_PRE1;
  mini[2] = 0x7FFF0000;
  f = c;
  f.ccb_Flags &= ~CCB_CCBPRE;
  f.ccb_SourcePtr = (CelData *)mini;
  tk(&f, 128);

  f = c;
  f.ccb_Flags |= CCB_SKIP;
  tk(&f, 128);

  /* one coded pixel, with its PLUT loaded */
  mini[4] = 0;
  mini[5] = 0;
  f = c;
  f.ccb_Flags |= CCB_LDPLUT;
  f.ccb_SourcePtr = (CelData *)(mini + 4);
  f.ccb_PLUTPtr = b_plut;
  f.ccb_PRE0 = PRE0_BPP_1;
  f.ccb_PRE1 = PRE1_TLLSB_PDC0;
  tk(&f, 128);
  f.ccb_PRE0 = PRE0_BPP_4;
  tk(&f, 128);
  f.ccb_PRE0 = PRE0_BPP_6;
  tk(&f, 128);
  f.ccb_PRE0 = PRE0_BPP_8;
  f.ccb_PRE1 = PRE1_TLLSB_PDC0;
  tk(&f, 128);
}

static void bitmap_dram(void)
{
  CCB c;
  void *buf;
  Item bi;
  Item saved;

  buf = AllocMem(SCREEN_W * SCREEN_H * 2, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  if(buf == NULL)
    {
      grid_u(999999);
      grid_u(999999);
      return;
    }
  bi = CreateBitmapVA(CBM_TAG_WIDTH, SCREEN_W, CBM_TAG_HEIGHT, SCREEN_H, CBM_TAG_BUFFER, buf, CBM_TAG_DONE);
  if(bi < 0)
    {
      grid_x((u32)bi, 8);
      grid_x((u32)bi, 8);
    }
  else
    {
      saved = target;
      target = bi;
      make16(&c, SCREEN_W, SCREEN_H, 0, 0);
      tk(&c, 1);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, 1);
      target = saved;
      DeleteItem(bi);
    }
  FreeMem(buf, SCREEN_W * SCREEN_H * 2);
}

static void vram_ops(void)
{
  u32 i;
  u32 t0;
  u32 n[3];
  u32 m;
  void *d;
  void *s;

  progress(L("PAGES VRAM", "VRAM PAGES"));
  d = b_sc.sc_Bitmaps[1]->bm_Buffer;
  s = b_sc.sc_Bitmaps[0]->bm_Buffer;
  n[0] = 1;
  n[1] = 16;
  n[2] = (u32)b_sc.sc_NumBitmapPages;
  for(m = 0; m < 3; m++)
    {
      stat_reset(&st);
      for(i = 0; i < REPS; i++)
        {
          t0 = usec_now();
          SetVRAMPages(b_vram_ioreq, d, 0, (s32)n[m], 0xFFFFFFFF);
          stat_add(&st, usec_now() - t0);
        }
      grid_u(stat_median(&st));
    }
  for(m = 0; m < 3; m++)
    {
      stat_reset(&st);
      for(i = 0; i < REPS; i++)
        {
          t0 = usec_now();
          CopyVRAMPages(b_vram_ioreq, d, s, n[m], 0xFFFFFFFF);
          stat_add(&st, usec_now() - t0);
        }
      grid_u(stat_median(&st));
    }
}

static void page1(void)
{
  CCB c;
  u32 i;
  u32 s;
  u32 w;
  void *vr;
  s32 co;
  s32 si;

  grid_begin('B', 5);
  ccb_costs();

  progress(L("DECOUPE", "CLIP   "));
  for(i = 0; i < 12; i++)
    {
      make16(&c, SCREEN_W, SCREEN_H, clips[i][0], clips[i][1]);
      tk(&c, 1);
    }
  for(i = 0; i < 6; i++)
    {
      make16(&c, SCREEN_W, SCREEN_H, sclips[i][0], sclips[i][1]);
      c.ccb_Flags |= CCB_ACSC | CCB_ALSC;
      tk(&c, 1);
    }

  progress(L("ECHELLES", "SCALES  "));
  for(i = 0; i < 8; i++)
    {
      s = scales[i];
      w = (SCREEN_W * 256) / s;
      if(w > SCREEN_W)
        w = SCREEN_W;
      make16(&c, w, SCREEN_H, 0, 0);
      c.ccb_HDX = (s32)(s << 12);
      tk(&c, 1);
    }
  for(i = 0; i < 8; i++)
    {
      s = scales[i];
      w = (SCREEN_H * 256) / s;
      if(w > SCREEN_H)
        w = SCREEN_H;
      make16(&c, SCREEN_W, w, 0, 0);
      c.ccb_VDY = (s32)(s << 8);
      tk(&c, 1);
    }
  make16(&c, SCREEN_W, SCREEN_H, 0, 0);
  c.ccb_HDX = 1 << 19;
  c.ccb_VDY = 1 << 15;
  tk(&c, 1);
  make16(&c, SCREEN_W / 2, SCREEN_H / 2, 0, 0);
  c.ccb_HDX = 1 << 21;
  c.ccb_VDY = 1 << 17;
  tk(&c, 1);

  progress("ROTATIONS");
  for(i = 0; i < 5; i++)
    {
      co = rots[i][0];
      si = rots[i][1];
      make16(&c, 160, 120, 0, 0);
      c.ccb_XPos = (Coord)((160 * 65536) - (co * 80 - si * 60));
      c.ccb_YPos = (Coord)((120 * 65536) - (si * 80 + co * 60));
      c.ccb_HDX  = co * 16;
      c.ccb_HDY  = si * 16;
      c.ccb_VDX  = -si;
      c.ccb_VDY  = co;
      tk(&c, 1);
    }

  progress("PIXC");
  for(i = 0; i < 8; i++)
    {
      make16(&c, SCREEN_W, SCREEN_H, 0, 0);
      c.ccb_PIXC = (u32)pixcs[i] * 0x00010001;
      tk(&c, 1);
    }
  for(i = 0; i < 4; i++)
    {
      c = b_ccbs[C_FILL];
      c.ccb_PIXC = (u32)pixcs[(i == 0) ? 0 : ((i == 1) ? 2 : ((i == 2) ? 3 : 5))] * 0x00010001;
      tk(&c, 1);
    }

  progress(L("DIVERS", "MISC  "));
  make16(&c, SCREEN_W, SCREEN_H, 319, 0);
  c.ccb_HDX = -(1 << 20);
  tk(&c, 1);
  make16(&c, SCREEN_W, SCREEN_H, 0, 239);
  c.ccb_VDY = -(1 << 16);
  tk(&c, 1);
  make16(&c, 160, 120, 100, 10);
  c.ccb_HDX = 1180482;
  c.ccb_HDY = 681574;
  c.ccb_VDX = -42598;
  c.ccb_VDY = 73782;
  c.ccb_HDDX = 2048;
  c.ccb_HDDY = -1024;
  tk(&c, 1);

  /* 160 x 240 with the rows contiguous, then spaced as in the 320-wide picture */
  make16(&c, 160, SCREEN_H, 0, 0);
  tk(&c, 1);
  make16(&c, 160, SCREEN_H, 0, 0);
  c.ccb_PRE1 = ((80 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
    PRE1_TLLSB_PDC0 | (160 - PRE1_TLHPCNT_PREFETCH);
  tk(&c, 1);

  vr = AllocMem(SCREEN_W * SCREEN_H * 2, MEMTYPE_VRAM | MEMTYPE_CEL);
  if(vr == NULL)
    grid_u(999999);
  else
    {
      memcpy(vr, b_src16, SCREEN_W * SCREEN_H * 2);
      make16(&c, SCREEN_W, SCREEN_H, 0, 0);
      c.ccb_SourcePtr = (CelData *)vr;
      tk(&c, 1);
      FreeMem(vr, SCREEN_W * SCREEN_H * 2);
    }

  progress(L("BITMAP EN DRAM", "BITMAP IN DRAM"));
  bitmap_dram();
  vram_ops();
  grid_end();
}

void group_sweep(int page)
{
  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  if(page == 0)
    page0();
  else
    page1();
  sw_free();
}

/* ---------------------------------------------------------------- version 3.1 */

/* What version 3.0 left unexplained: ACE on narrow cels, the row count with ACE, the
   frame buffer reads of blends and what off-screen cels cost. */
static void page3a(void)
{
  static const u16 w3[15] = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 32 };
  static const u16 h3[8]  = { 1, 2, 3, 4, 8, 16, 60, 240 };
  static const u8  n3[3]  = { 1, 8, 64 };
  CCB c;
  u32 i;
  u32 t;

  grid_begin('D', 4);
  progress(L("LARGEURS SANS ACE", "WIDTHS, NO ACE   "));
  for(i = 0; i < 15; i++)
    {
      make16(&c, w3[i], SCREEN_H, 0, 0);
      tk(&c, k_for(w3[i], SCREEN_H));
    }
  progress(L("LARGEURS AVEC ACE", "WIDTHS WITH ACE  "));
  for(i = 0; i < 15; i++)
    {
      make16(&c, w3[i], SCREEN_H, 0, 0);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, k_for(w3[i], SCREEN_H));
    }
  progress(L("HAUTEURS AVEC ACE", "HEIGHTS WITH ACE "));
  for(i = 0; i < 8; i++)
    {
      make16(&c, SCREEN_W, h3[i], 0, 0);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, k_for(SCREEN_W, h3[i]));
    }
  progress(L("PAQUETS AVEC ACE", "PACKETS WITH ACE"));
  for(t = 0; t < 3; t++)
    for(i = 0; i < 3; i++)
      {
        make_packed(&c, (t == 0) ? PACK_LITERAL : ((t == 1) ? PACK_PACKED : PACK_TRANSPARENT), n3[i]);
        c.ccb_Flags |= CCB_ACE;
        tk(&c, 1);
      }
  grid_end();
}

static void page3b(void)
{
  static const u16 wb[6]  = { 8, 16, 32, 64, 128, 320 };
  static const u16 hb[3]  = { 8, 60, 240 };
  static const u16 fw[3]  = { 16, 64, 320 };
  static const u16 fh[3]  = { 16, 64, 240 };
  static const s16 off[4][2] = { { 400, 0 }, { -400, 0 }, { 0, 300 }, { 0, -300 } };
  CCB c;
  u32 i;
  u32 j;
  u32 a;

  grid_begin('E', 4);
  progress(L("MELANGES", "BLENDS  "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 6; i++)
      {
        make16(&c, wb[i], SCREEN_H, 0, 0);
        c.ccb_PIXC = PIXC_HALF;
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, k_for(wb[i], SCREEN_H));
      }
  for(a = 0; a < 2; a++)
    for(i = 0; i < 3; i++)
      {
        make16(&c, SCREEN_W, hb[i], 0, 0);
        c.ccb_PIXC = PIXC_HALF;
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, k_for(SCREEN_W, hb[i]));
      }
  progress(L("APLATS MELANGES", "BLENDED FILLS  "));
  for(i = 0; i < 3; i++)
    for(j = 0; j < 3; j++)
      {
        c = b_ccbs[C_FILL];
        c.ccb_HDX  = (s32)((u32)fw[i] << 20);
        c.ccb_VDY  = (s32)((u32)fh[j] << 16);
        c.ccb_PIXC = PIXC_HALF;
        tk(&c, k_for(fw[i], fh[j]));
      }
  progress(L("HORS ECRAN", "OFF SCREEN"));
  for(i = 0; i < 4; i++)
    {
      make16(&c, SCREEN_W, SCREEN_H, off[i][0], off[i][1]);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, 1);
    }
  for(a = 0; a < 2; a++)
    for(i = 0; i < 4; i++)
      {
        make16(&c, 64, 64, off[i][0], off[i][1]);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress(L("TRANSPARENCE AVEC ACE", "TRANSPARENCY WITH ACE"));
  for(i = 0; i < 5; i++)
    {
      make_trans(&c, i);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, 1);
    }
  grid_end();
}

/* ---------------------------------------------------------------- version 3.2 */

/* cels of 64 x 64 reduced, enlarged and turned, centred on the screen, without and with ACE */
static s32 mul1616(s32 a, s32 b)
{
  return (a >> 8) * (b >> 8);
}

static void place(CCB *c, s32 scale, s32 cs, s32 sn)
{
  s32 hx;
  s32 hy;

  make16(c, 64, 64, 0, 0);
  hx = mul1616(scale, cs);
  hy = mul1616(scale, sn);
  c->ccb_HDX = hx * 16;
  c->ccb_HDY = hy * 16;
  c->ccb_VDX = -hy;
  c->ccb_VDY = hx;
  c->ccb_XPos = (Coord)((160 << 16) - 32 * (hx - hy));
  c->ccb_YPos = (Coord)((120 << 16) - 32 * (hy + hx));
}

void group_scale(void)
{
  /* 0.25 0.5 0.71 1 1.41 2 3, then cosine and sine of 30, 45, 60 and 94 degrees */
  static const s32 sc[7] = { 16384, 32768, 46341, 65536, 92682, 131072, 196608 };
  static const s32 rot[4][2] = { { 56756, 32768 }, { 46341, 46341 }, { 32768, 56756 }, { -4571, 65376 } };
  static const u8 which[3] = { 3, 4, 2 };
  CCB c;
  u32 a;
  u32 i;
  u32 j;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('X', 4);
  progress(L("ECHELLES", "SCALES  "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 7; i++)
      {
        place(&c, sc[i], 65536, 0);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress("ROTATIONS");
  for(j = 0; j < 3; j++)
    for(a = 0; a < 2; a++)
      for(i = 0; i < 4; i++)
        {
          place(&c, sc[which[j]], rot[i][0], rot[i][1]);
          if(a != 0)
            c.ccb_Flags |= CCB_ACE;
          tk(&c, 8);
        }
  progress(L("MELANGES", "BLENDS  "));
  for(a = 0; a < 2; a++)
    {
      place(&c, 65536, 65536, 0);
      c.ccb_PIXC = PIXC_HALF;
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
      place(&c, 65536, rot[1][0], rot[1][1]);
      c.ccb_PIXC = PIXC_HALF;
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
    }
  grid_end();
  sw_free();
}

void group_sweep3(int page)
{
  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  if(page == 0)
    page3a();
  else
    page3b();
  sw_free();
}

/* ---------------------------------------------------------------- version 3.3 */

/* All the numbers below are in the order of the calls, as on the other pages. */

/* CELS COUPES: a 300 x 300 picture of 16-bit pixels, shown in part or whole, at scale 1.41,
   1 and 0.5, each case without ACE then with ACE. The background of the wizard scene of
   Strahl (300 x 300, scale 1.41) holds 11 of the 12 ms the core is short of. The table says
   what is outside the screen in each case: nothing, columns on one side, rows on one side. */
#define BIG_W      300
#define BIG_WORDS  150

typedef struct Cut
{
  s16 w;
  s16 h;
  s16 x;
  s16 y;
  s32 sc;
  u32 fl;
  u8  flip;   /* 1: rows drawn right to left, 2: picture drawn bottom to top */
} Cut;

#define S141   92682
#define S100   65536
#define S050   32768
#define SCLIP  (CCB_ACSC | CCB_ALSC)

static const Cut cuts[] =
  {
    /* scale 1.41, the screen is 226 x 170 source pixels */
    { 150, 150,    0,    0, S141, 0 },     /*  0 all inside */
    { 226, 150,    0,    0, S141, 0 },     /*  1 all inside, as wide as the screen */
    { 300, 150,    0,    0, S141, 0 },     /*  2 74 columns out on the right */
    { 300, 150, -104,    0, S141, 0 },     /*  3 74 columns out on the left */
    { 300, 150,  -52,    0, S141, 0 },     /*  4 37 columns out on each side */
    { 150, 170,    0,    0, S141, 0 },     /*  5 all inside, as high as the screen */
    { 150, 300,    0,    0, S141, 0 },     /*  6 130 rows out below */
    { 150, 300,    0, -184, S141, 0 },     /*  7 130 rows out above */
    { 150, 300,    0,  -92, S141, 0 },     /*  8 65 rows out on each side */
    { 226, 170,    0,    0, S141, 0 },     /*  9 exactly the screen */
    { 300, 170,  -52,    0, S141, 0 },     /* 10 full screen, columns out */
    { 226, 300,    0,  -92, S141, 0 },     /* 11 full screen, rows out */
    { 300, 300,  -52,  -92, S141, 0 },     /* 12 both: the scene's background, not turned */
    { 300, 300,  400,    0, S141, 0 },     /* 13 all out, right */
    { 300, 300, -500,    0, S141, 0 },     /* 14 all out, left */
    { 300, 300,    0,  300, S141, 0 },     /* 15 all out, below */
    { 300, 300,    0, -500, S141, 0 },     /* 16 all out, above */
    /* scale 1 */
    { 150, 150,    0,    0, S100, 0 },     /* 17 all inside */
    { 300, 150,    0,    0, S100, 0 },     /* 18 all inside */
    { 300, 150,  160,    0, S100, 0 },     /* 19 140 columns out on the right */
    { 300, 150, -140,    0, S100, 0 },     /* 20 140 columns out on the left */
    { 150, 240,    0,    0, S100, 0 },     /* 21 all inside */
    { 150, 300,    0,    0, S100, 0 },     /* 22 60 rows out below */
    { 150, 300,    0,  -60, S100, 0 },     /* 23 60 rows out above */
    { 150, 300,    0,  -30, S100, 0 },     /* 24 30 rows out on each side */
    { 300, 240,   10,    0, S100, 0 },     /* 25 all inside */
    { 300, 300,   10,  -30, S100, 0 },     /* 26 the scene's background at scale 1 */
    /* scale 0.5, the whole picture is 150 x 150 on the screen */
    { 300, 300,    0,    0, S050, 0 },     /* 27 all inside */
    { 300, 300,  245,    0, S050, 0 },     /* 28 the right half out */
    { 300, 300,  -75,    0, S050, 0 },     /* 29 the left half out */
    { 300, 300,    0,  165, S050, 0 },     /* 30 the lower half out */
    { 300, 300,    0,  -75, S050, 0 },     /* 31 the upper half out */
    /* cases 12 and 2 with the flags that give up what is out of the screen */
    { 300, 300,  -52,  -92, S141, SCLIP }, /* 32 */
    { 300, 150,    0,    0, S141, SCLIP }, /* 33 */
    /* case 9 mirrored: the background of the scene is drawn right to left */
    { 226, 170,  319,    0, S141, 0, 1 },  /* 34 mirrored in X */
    { 226, 170,    0,  239, S141, 0, 2 },  /* 35 mirrored in Y */
    { 226, 170,  319,  239, S141, 0, 3 }   /* 36 both */
  };

#define NCUTS (sizeof(cuts) / sizeof(cuts[0]))

static void fill_big(void)
{
  u32 x;
  u32 y;
  u16 *p;

  p = (u16 *)gen;
  for(y = 0; y < BIG_W; y++)
    for(x = 0; x < BIG_W; x++)
      p[y * BIG_W + x] = (u16)MakeRGB15((x >> 5) & 31, (y & 7) * 4, 8 + (x & 15));
}

static void make_big(CCB *c, u32 w, u32 h, s32 x, s32 y, s32 sc)
{
  init_ccb(c, flags16, gen, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((h - 1) << PRE0_VCNT_SHIFT),
           ((BIG_WORDS - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (w - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  c->ccb_XPos   = (Coord)(x * 65536);
  c->ccb_YPos   = (Coord)(y * 65536);
  c->ccb_HDX    = sc * 16;
  c->ccb_VDY    = sc;
  c->ccb_Width  = (s32)w;
  c->ccb_Height = (s32)h;
}

void group_cut(void)
{
  CCB c;
  u32 i;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress(L("PREPARATION", "PREPARING  "));
  fill_big();
  grid_begin('K', 5);
  progress(L("COUPES", "CLIPS "));
  for(i = 0; i < NCUTS; i++)
    for(a = 0; a < 2; a++)
      {
        make_big(&c, cuts[i].w, cuts[i].h, cuts[i].x, cuts[i].y, cuts[i].sc);
        c.ccb_Flags |= cuts[i].fl;
        if(cuts[i].flip & 1)
          c.ccb_HDX = -c.ccb_HDX;
        if(cuts[i].flip & 2)
          c.ccb_VDY = -c.ccb_VDY;
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 1);
      }
  grid_end();
  sw_free();
}

/* a cel already built keeps its source and its format, with another size */
static void resize(CCB *c, u32 w, u32 h)
{
  c->ccb_PRE0 = (c->ccb_PRE0 & ~PRE0_VCNT_MASK) | ((h - 1) << PRE0_VCNT_SHIFT);
  c->ccb_PRE1 = (c->ccb_PRE1 & ~PRE1_TLHPCNT_MASK) | (w - PRE1_TLHPCNT_PREFETCH);
  c->ccb_Width  = (s32)w;
  c->ccb_Height = (s32)h;
}

/* centred on the screen, sx and sy in 16.16 */
static void centre(CCB *c, s32 sx, s32 sy)
{
  c->ccb_HDX  = sx * 16;
  c->ccb_VDY  = sy;
  c->ccb_XPos = (Coord)((160 << 16) - 32 * sx);
  c->ccb_YPos = (Coord)((120 << 16) - 32 * sy);
}

/* CELS ECHELLE XY: lists of 8 cels of 64 x 64 as on the page CELS ECHELLE, each group
   without ACE then with ACE:
    0-11  scale in X only, 0.25 0.5 0.71 1.41 2 3
   12-23  scale in Y only
   24-31  X 2 Y 0.5, X 0.5 Y 2, X 3 Y 0.25, X 0.25 Y 3
   32-37  half blend, scale 0.5, 2, 3
   38-43  8 bits coded, scale 0.5, 1, 2
   44-59  the same scale in X and Y: 0.6 0.85 1.1 1.2 1.3 1.5 1.7 2.5 */
void group_xy(void)
{
  static const s32 sc[6] = { 16384, 32768, 46341, 92682, 131072, 196608 };
  static const s32 mix[4][2] =
    { { 131072, 32768 }, { 32768, 131072 }, { 196608, 16384 }, { 16384, 196608 } };
  static const u8 blend[3] = { 1, 4, 5 };
  static const s32 s8[3] = { 32768, 65536, 131072 };
  static const s32 fine[8] = { 39322, 55706, 72090, 78643, 85197, 98304, 111411, 163840 };
  CCB c;
  CCB t;
  u32 a;
  u32 i;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('Y', 4);
  progress(L("X SEUL", "X ONLY"));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 6; i++)
      {
        make16(&c, 64, 64, 0, 0);
        centre(&c, sc[i], 65536);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress(L("Y SEUL", "Y ONLY"));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 6; i++)
      {
        make16(&c, 64, 64, 0, 0);
        centre(&c, 65536, sc[i]);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress(L("X ET Y", "X + Y "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 4; i++)
      {
        make16(&c, 64, 64, 0, 0);
        centre(&c, mix[i][0], mix[i][1]);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress(L("MELANGES", "BLENDS  "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 3; i++)
      {
        make16(&c, 64, 64, 0, 0);
        centre(&c, sc[blend[i]], sc[blend[i]]);
        c.ccb_PIXC = PIXC_HALF;
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress("8 BITS");
  make_unpacked(&t, PRE0_BPP_8, 8, 0);
  for(a = 0; a < 2; a++)
    for(i = 0; i < 3; i++)
      {
        c = t;
        resize(&c, 64, 64);
        centre(&c, s8[i], s8[i]);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  progress(L("ECHELLES FINES", "FINE SCALES   "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 8; i++)
      {
        make16(&c, 64, 64, 0, 0);
        centre(&c, fine[i], fine[i]);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  grid_end();
  sw_free();
}

/* CELS LARGEURS, unpacked cels 240 rows high, width by width: the time per row goes up by
   steps that the core does not put at the same widths.
   Page 1, grid L, 16 bits: W = 1..32, then 36 40 44 48 56 64 80 96 112 128 160 192 224;
   the 45 values without ACE, then the 45 with ACE.
   Page 2, grid N: 8 bits coded W = 4, 8 .. 128 without ACE, with ACE (a word = 4 pixels),
   then 6 bits coded W = 5 10 16 21 26 32 37 42 48 53 58 64 128 without ACE, with ACE.
   Page 3, grid H, 16 bits: W = 2 4 8 16 32 by H = 1 2 4 8 16 60 120 without ACE (35 values),
   with ACE (35), then W = 4 8 .. 32, H = 240, rows contiguous in memory, without and with ACE. */
static void w16(u32 w, u32 a)
{
  CCB c;

  make16(&c, w, SCREEN_H, 0, 0);
  if(a != 0)
    c.ccb_Flags |= CCB_ACE;
  tk(&c, k_for(w, SCREEN_H));
}

static void w_other(const CCB *t, u32 w, u32 a)
{
  CCB c;

  c = *t;
  resize(&c, w, SCREEN_H);
  if(a != 0)
    c.ccb_Flags |= CCB_ACE;
  tk(&c, k_for(w, SCREEN_H));
}

void group_widths(int page)
{
  static const u8 more[13] = { 36, 40, 44, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224 };
  static const u8 w6[13]   = { 5, 10, 16, 21, 26, 32, 37, 42, 48, 53, 58, 64, 128 };
  static const u8 hw[5]    = { 2, 4, 8, 16, 32 };
  static const u8 hh[7]    = { 1, 2, 4, 8, 16, 60, 120 };
  CCB c;
  CCB t;
  u32 a;
  u32 i;
  u32 j;
  u32 w;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  if(page == 0)
    {
      grid_begin('L', 5);
      progress("16 BITS");
      for(a = 0; a < 2; a++)
        {
          for(w = 1; w <= 32; w++)
            w16(w, a);
          for(i = 0; i < 13; i++)
            w16(more[i], a);
        }
      grid_end();
    }
  else if(page == 1)
    {
      grid_begin('N', 5);
      progress("8 BITS");
      make_unpacked(&t, PRE0_BPP_8, 8, 0);
      for(a = 0; a < 2; a++)
        for(i = 1; i <= 32; i++)
          w_other(&t, i * 4, a);
      progress("6 BITS");
      make_unpacked(&t, PRE0_BPP_6, 6, 0);
      for(a = 0; a < 2; a++)
        for(i = 0; i < 13; i++)
          w_other(&t, w6[i], a);
      grid_end();
    }
  else
    {
      grid_begin('H', 5);
      progress(L("HAUTEURS", "HEIGHTS "));
      for(a = 0; a < 2; a++)
        for(i = 0; i < 5; i++)
          for(j = 0; j < 7; j++)
            {
              make16(&c, hw[i], hh[j], 0, 0);
              if(a != 0)
                c.ccb_Flags |= CCB_ACE;
              tk(&c, k_for(hw[i], hh[j]));
            }
      progress(L("LIGNES CONTIGUES", "CONTIGUOUS ROWS "));
      for(a = 0; a < 2; a++)
        for(w = 4; w <= 32; w += 4)
          {
            make16(&c, w, SCREEN_H, 0, 0);
            c.ccb_PRE1 = ((w / 2 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
              PRE1_TLLSB_PDC0 | (w - PRE1_TLHPCNT_PREFETCH);
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, k_for(w, SCREEN_H));
          }
      grid_end();
    }
  sw_free();
}

/* CELS TRANSPARENTS, 320 x 240, grid Q. A row per layout of the transparent pixels, four
   values per row: 6 bits coded without ACE, with ACE, 16 bits without ACE, with ACE.
   Layouts: 0 none, 1 alternate 1:1, 2 by 2, 3 by 4, 4 by 8, 5 by 16, 6 by 32,
   7 one opaque in 4, 8 one transparent in 4, 9 random half, 10 all transparent,
   11 one row out of two transparent, 12 one row out of four opaque.
   Then 6 bits, layouts 0, 4 and 10, scale 0.5 then scale 2 (the cel is 160 x 120 at scale 2),
   without ACE, with ACE. */
static u32 hole_at(u32 p, u32 x, u32 y, u32 *rnd)
{
  switch(p)
    {
    case 0:  return 0;
    case 1:  return x & 1;
    case 2:  return (x >> 1) & 1;
    case 3:  return (x >> 2) & 1;
    case 4:  return (x >> 3) & 1;
    case 5:  return (x >> 4) & 1;
    case 6:  return (x >> 5) & 1;
    case 7:  return (x & 3) != 0;
    case 8:  return (x & 3) == 3;
    case 9:  return rand_next(rnd) >> 31;
    case 11: return y & 1;
    case 12: return (y & 3) != 0;
    default: return 1;
    }
}

static void make_holes(CCB *c, u32 p, u32 is16)
{
  u32 x;
  u32 y;
  u32 pos;
  u32 v;
  u32 rnd;
  u32 wpr;

  wpr = is16 ? 160 : 60;
  memset(gen, 0, wpr * 8 * 4);
  rnd = 991 + p;
  for(y = 0; y < 8; y++)
    {
      pos = y * wpr * 32;
      for(x = 0; x < SCREEN_W; x++)
        {
          if(hole_at(p, x, y, &rnd))
            v = 0;
          else if(is16)
            v = MakeRGB15(3 + (x & 3), 2 + (y & 3), 6);
          else
            v = 1 + (x % 31);
          put_bits(gen, &pos, v, is16 ? 16 : 6);
        }
    }
  for(y = 8; y < SCREEN_H; y++)
    memcpy(gen + y * wpr, gen + (y & 7) * wpr, wpr * 4);
  if(is16)
    init_ccb(c, flags16 & ~(CCB_BGND | CCB_NOBLK), gen, NULL,
             PRE0_LINEAR | PRE0_BPP_16 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
             ((wpr - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
             PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  else
    init_ccb(c, (flags16 | CCB_LDPLUT) & ~(CCB_BGND | CCB_NOBLK), gen, plut_tr,
             PRE0_BPP_6 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
             ((wpr - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
             PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
}

void group_holes(void)
{
  static const u8 three[3] = { 0, 4, 10 };
  CCB c;
  CCB t;
  u32 p;
  u32 f;
  u32 a;
  u32 s;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('Q', 4);
  progress(L("MOTIFS", "SHAPES"));
  for(p = 0; p < 13; p++)
    for(f = 0; f < 2; f++)
      {
        make_holes(&t, p, f);
        for(a = 0; a < 2; a++)
          {
            c = t;
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, 1);
          }
      }
  progress(L("ECHELLES", "SCALES  "));
  for(p = 0; p < 3; p++)
    {
      make_holes(&t, three[p], 0);
      for(s = 0; s < 2; s++)
        for(a = 0; a < 2; a++)
          {
            c = t;
            if(s == 0)
              {
                c.ccb_HDX = 1 << 19;
                c.ccb_VDY = 1 << 15;
              }
            else
              {
                resize(&c, 160, 120);
                c.ccb_HDX = 1 << 21;
                c.ccb_VDY = 1 << 17;
              }
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, 1);
          }
    }
  grid_end();
  sw_free();
}

/* ---------------------------------------------------------------- version 3.4 */

#define H10 (1 << 20)
#define V10 (1 << 16)

/* the layout without holes of CELS TRANSPARENTS, 6 or 16 bits, with the given size, place and
   slopes, without then with ACE. The source is made again each time: the two formats do not fit
   together in gen. */
static void pos_one(u32 is16, u32 w, u32 h, s32 x, s32 y, s32 hdx, s32 vdx, s32 vdy, u32 pixc, u32 k)
{
  CCB c;
  u32 a;

  make_holes(&c, 0, is16);
  resize(&c, w, h);
  c.ccb_XPos = (Coord)(x * 65536);
  c.ccb_YPos = (Coord)(y * 65536);
  c.ccb_HDX  = hdx;
  c.ccb_VDX  = vdx;
  c.ccb_VDY  = vdy;
  c.ccb_PIXC = pixc;
  for(a = 0; a < 2; a++)
    {
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, k);
    }
}

/* CELS ACE POSITION, grid J, four values per row: 16 bits without ACE, with ACE, 6 bits without
   ACE, with ACE. Do the two corner engines always share a write cycle, or only when their two
   pixels are in the same 32-bit word of the frame buffer (the two lines of a pair)?
    0- 3  320 x 236 at y = 0, 1, 2, 3
    4- 5  8 cels of 32 x 32 at y = 0, 1
    6- 8  320 x 240 mirrored in X, in Y, in both
    9-10  320 x 236, half blend, at y = 0, 1
   11-16  8 cels of 64 x 64 centred, each row shifted from the row above by
          0, 1/4, 1/2, 1, 2, -1 pixel */
void group_acepos(void)
{
  static const s32 sh[6] = { 0, 16384, 32768, 65536, 131072, -65536 };
  u32 i;
  u32 f;
  s32 x0;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('J', 4);
  progress(L("Y PAIR ET IMPAIR", "Y EVEN AND ODD  "));
  for(i = 0; i < 4; i++)
    for(f = 0; f < 2; f++)
      pos_one(1 - f, SCREEN_W, 236, 0, (s32)i, H10, 0, V10, PIXC_SOLID, 1);
  progress(L("PETITS CELS", "SMALL CELS "));
  for(i = 0; i < 2; i++)
    for(f = 0; f < 2; f++)
      pos_one(1 - f, 32, 32, 0, (s32)i, H10, 0, V10, PIXC_SOLID, 8);
  progress(L("MIROIRS", "MIRRORS"));
  for(f = 0; f < 2; f++)
    pos_one(1 - f, SCREEN_W, SCREEN_H, SCREEN_W, 0, -H10, 0, V10, PIXC_SOLID, 1);
  for(f = 0; f < 2; f++)
    pos_one(1 - f, SCREEN_W, SCREEN_H, 0, SCREEN_H, H10, 0, -V10, PIXC_SOLID, 1);
  for(f = 0; f < 2; f++)
    pos_one(1 - f, SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H, -H10, 0, -V10, PIXC_SOLID, 1);
  progress(L("MELANGES", "BLENDS  "));
  for(i = 0; i < 2; i++)
    for(f = 0; f < 2; f++)
      pos_one(1 - f, SCREEN_W, 236, 0, (s32)i, H10, 0, V10, PIXC_HALF, 1);
  progress(L("LIGNES DECALEES", "SHIFTED ROWS   "));
  for(i = 0; i < 6; i++)
    for(f = 0; f < 2; f++)
      {
        x0 = 128 - (sh[i] >> 11);
        pos_one(1 - f, 64, 64, x0, 88, H10, sh[i], V10, PIXC_SOLID, 8);
      }
  grid_end();
  sw_free();
}

/* CELS SOURCE, grid U, 16 bits, five values per row.
    0-63  widths 2, 4 .. 32 without ACE, the source starting 0, 1, 2, then 3 words after a
          multiple of 4 words
   64-79  widths 2, 4 .. 32 without ACE, half blend: the drawing is slower than the reading
   80-89  source in VRAM, widths 8, 16, 32, 64, 320, without ACE then with ACE */
void group_source(void)
{
  static const u16 wv[5] = { 8, 16, 32, 64, 320 };
  CCB c;
  u16 *base;
  void *vr;
  u32 o;
  u32 w;
  u32 a;
  u32 i;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  base = (u16 *)(((u32)b_src16 + 15) & ~(u32)15);
  grid_begin('U', 5);
  progress(L("ALIGNEMENT", "ALIGNMENT "));
  for(o = 0; o < 4; o++)
    for(w = 2; w <= 32; w += 2)
      {
        make16(&c, w, SCREEN_H, 0, 0);
        c.ccb_SourcePtr = (CelData *)(base + o * 2);
        tk(&c, k_for(w, SCREEN_H));
      }
  progress(L("MELANGE", "BLEND  "));
  for(w = 2; w <= 32; w += 2)
    {
      make16(&c, w, SCREEN_H, 0, 0);
      c.ccb_SourcePtr = (CelData *)base;
      c.ccb_PIXC = PIXC_HALF;
      tk(&c, k_for(w, SCREEN_H));
    }
  progress(L("SOURCE EN VRAM", "SOURCE IN VRAM"));
  vr = AllocMem(SCREEN_W * SCREEN_H * 2, MEMTYPE_VRAM | MEMTYPE_CEL);
  if(vr == NULL)
    grid_u(999999);
  else
    {
      memcpy(vr, b_src16, SCREEN_W * SCREEN_H * 2);
      for(a = 0; a < 2; a++)
        for(i = 0; i < 5; i++)
          {
            make16(&c, wv[i], SCREEN_H, 0, 0);
            c.ccb_SourcePtr = (CelData *)vr;
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, k_for(wv[i], SCREEN_H));
          }
      FreeMem(vr, SCREEN_W * SCREEN_H * 2);
    }
  grid_end();
  sw_free();
}

static u32 pk_value(u32 x, u32 bits)
{
  if(bits == 16)
    return (u32)MakeRGB15(3 + (x & 3), 2 + ((x >> 2) & 3), 6);
  return 1 + (x % 31);
}

/* packed rows as make_packed, 8 or 16 bits per pixel: the offset that starts a row is then
   two bytes long */
static void make_packed_w(CCB *c, u32 type, u32 n, u32 bits)
{
  u32 x;
  u32 m;
  u32 k;
  u32 y;
  u32 pos;
  u32 words;

  pos = 0;
  memset(gen, 0, 4096);
  put_bits(gen, &pos, 0, 16);
  for(x = 0; x < SCREEN_W; x += m)
    {
      m = SCREEN_W - x;
      if(m > n)
        m = n;
      put_bits(gen, &pos, type, 2);
      put_bits(gen, &pos, m - 1, 6);
      if(type == PACK_PACKED)
        put_bits(gen, &pos, pk_value(x, bits), bits);
      else if(type == PACK_LITERAL)
        for(k = 0; k < m; k++)
          put_bits(gen, &pos, pk_value(x + k, bits), bits);
    }
  put_bits(gen, &pos, PACK_EOL, 2);
  words = (pos + 31) >> 5;
  if(words < 2)
    words = 2;
  gen[0] |= (words - 2) << 16;
  for(y = 1; y < SCREEN_H; y++)
    memcpy(gen + y * words, gen, words * 4);
  init_ccb(c, flagspk, gen, plut_tr,
           ((bits == 16) ? (PRE0_LINEAR | PRE0_BPP_16) : PRE0_BPP_8) | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           0, PIXC_SOLID);
}

/* the same cel with ACE and LCE, one value */
static void lce_one(u32 is16, u32 w, u32 h, s32 x, s32 y, s32 vdx, u32 pixc, u32 k)
{
  CCB c;

  make_holes(&c, 0, is16);
  resize(&c, w, h);
  c.ccb_XPos = (Coord)(x * 65536);
  c.ccb_YPos = (Coord)(y * 65536);
  c.ccb_VDX  = vdx;
  c.ccb_PIXC = pixc;
  c.ccb_Flags |= CCB_ACE | CCB_LCE;
  tk(&c, k);
}

/* CELS PAQUETS ACE, grid V.
   Page 1, five values per row, 320 x 240:
    0-20  packed 6 bits with ACE: literal, repeated, then transparent packets of
          1, 2, 4, 8, 16, 32, 64 pixels
   21-38  packed 8 bits: literal, repeated, transparent packets of 4, 16, 64 pixels, each without
          then with ACE
   39-56  the same in 16 bits
   57-65  ACE + LCE: packed 6 bits, literal, repeated, transparent packets of 1, 8, 64 pixels
   66-69  ACE + LCE: 320 x 236 at y = 0 then y = 1, each 16 bits then 6 bits
   70-71  ACE + LCE: half blend, 16 bits then 6 bits
   72-73  ACE + LCE: 8 cels of 64 x 64, each row shifted by 1 pixel, 16 bits then 6 bits
   74-76  ACE + LCE: 8 cels of 64 x 64, 16 bits, scale 0.5, 1.41, 2
   Page 2, four values per row:
    0-23  6 bits, layouts no hole, 8:8, all transparent, scale 0.5, 0.71, 1.41, 2 (at 1.41 and 2
          the cel fills the screen), each without then with ACE
   24-47  the same in 16 bits
   48-59  8 cels of 64 x 64, 16 bits, the same scale in X and Y: 0.9, 0.95, 1.05, 1.1, 1.15, 1.25,
          each without then with ACE
   60-67  the same, X only at 0.85 and 1.1, then Y only at 0.85 and 1.1 */
void group_packs34(int page)
{
  static const u8 nw[3]    = { 4, 16, 64 };
  static const u8 three[3] = { 0, 4, 10 };
  static const u8 pty[3]   = { PACK_LITERAL, PACK_PACKED, PACK_TRANSPARENT };
  static const s32 sc4[4]  = { 32768, 46341, 92682, 131072 };
  static const s32 fine[6] = { 58982, 62259, 68813, 72090, 75366, 81920 };
  static const s32 axis[2] = { 55706, 72090 };
  static const u8 n3k[3]   = { 1, 8, 64 };
  static const s32 lsc[3]  = { 32768, 92682, 131072 };
  CCB c;
  CCB t;
  u32 i;
  u32 k;
  u32 b;
  u32 f;
  u32 s;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  if(page == 0)
    {
      grid_begin('V', 5);
      progress(L("6 BITS AVEC ACE", "6 BITS WITH ACE"));
      for(k = 0; k < 3; k++)
        for(i = 0; i < 7; i++)
          {
            make_packed(&c, pty[k], pks[i]);
            c.ccb_Flags |= CCB_ACE;
            tk(&c, 1);
          }
      progress(L("8 ET 16 BITS", "8 + 16 BITS "));
      for(b = 8; b <= 16; b += 8)
        for(k = 0; k < 3; k++)
          for(i = 0; i < 3; i++)
            {
              make_packed_w(&c, pty[k], nw[i], b);
              tk(&c, 1);
              c.ccb_Flags |= CCB_ACE;
              tk(&c, 1);
            }
      progress(L("ACE ET LCE", "ACE + LCE "));
      for(k = 0; k < 3; k++)
        for(i = 0; i < 3; i++)
          {
            make_packed(&c, pty[k], n3k[i]);
            c.ccb_Flags |= CCB_ACE | CCB_LCE;
            tk(&c, 1);
          }
      for(i = 0; i < 2; i++)
        for(f = 0; f < 2; f++)
          lce_one(1 - f, SCREEN_W, 236, 0, (s32)i, 0, PIXC_SOLID, 1);
      for(f = 0; f < 2; f++)
        lce_one(1 - f, SCREEN_W, 236, 0, 0, 0, PIXC_HALF, 1);
      for(f = 0; f < 2; f++)
        lce_one(1 - f, 64, 64, 96, 88, 65536, PIXC_SOLID, 8);
      for(i = 0; i < 3; i++)
        {
          make16(&c, 64, 64, 0, 0);
          centre(&c, lsc[i], lsc[i]);
          c.ccb_Flags |= CCB_ACE | CCB_LCE;
          tk(&c, 8);
        }
      grid_end();
    }
  else
    {
      grid_begin('V', 4);
      progress(L("TROUS ET ECHELLES", "HOLES AND SCALES "));
      for(f = 0; f < 2; f++)
        for(k = 0; k < 3; k++)
          {
            make_holes(&t, three[k], f);
            for(s = 0; s < 4; s++)
              for(a = 0; a < 2; a++)
                {
                  c = t;
                  if(sc4[s] > 65536)
                    resize(&c, (u32)(20971520 / sc4[s]), (u32)(15728640 / sc4[s]));
                  c.ccb_HDX = sc4[s] * 16;
                  c.ccb_VDY = sc4[s];
                  if(a != 0)
                    c.ccb_Flags |= CCB_ACE;
                  tk(&c, 1);
                }
          }
      progress(L("ECHELLES FINES", "FINE SCALES   "));
      for(s = 0; s < 6; s++)
        for(a = 0; a < 2; a++)
          {
            make16(&c, 64, 64, 0, 0);
            centre(&c, fine[s], fine[s]);
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, 8);
          }
      for(k = 0; k < 2; k++)
        for(s = 0; s < 2; s++)
          for(a = 0; a < 2; a++)
            {
              make16(&c, 64, 64, 0, 0);
              if(k == 0)
                centre(&c, axis[s], 65536);
              else
                centre(&c, 65536, axis[s]);
              if(a != 0)
                c.ccb_Flags |= CCB_ACE;
              tk(&c, 8);
            }
      grid_end();
    }
  sw_free();
}

/* ---------------------------------------------------------------- version 3.5 */

/* CELS CHEMIN GENERAL (grid S): lists of 8 cels of 64 x 64 centred as on CELS ECHELLE, without ACE then with ACE.
   Coded sources are the top-left 64 x 64 of a 320 x 240 picture (rows 320 pixels apart).
    0-13  8 bits coded, scale 0.33 0.5 0.71 0.85 1 1.41 2, without ACE then with ACE
   14-27  6 bits coded, the same
   28-33  16 bits, scale 0.33 0.6 0.85, without ACE then with ACE
   34-41  8 bits coded, rotation 30 and 45 degrees at scale 1, then at scale 0.71; without ACE (4) then with ACE (4)
   42-45  16 bits at scale 1 in perspective: rows narrowing to half (HDDX), then also slanting (HDDY); without, then with ACE */
static void place_any(CCB *c, u32 code, u32 bits, s32 scale, s32 cs, s32 sn)
{
  s32 hx;
  s32 hy;
  u32 wpr;

  if(bits == 16)
    make16(c, 64, 64, 0, 0);
  else
    {
      make_unpacked(c, code, bits, 0);
      wpr = (SCREEN_W * bits + 31) / 32;
      c->ccb_PRE0 = code | (63 << PRE0_VCNT_SHIFT);
      c->ccb_PRE1 = ((wpr - PRE1_WOFFSET_PREFETCH) << ((bits >= 8) ? PRE1_WOFFSET10_SHIFT : PRE1_WOFFSET8_SHIFT)) |
                    PRE1_TLLSB_PDC0 | (64 - PRE1_TLHPCNT_PREFETCH);
      c->ccb_Width  = 64;
      c->ccb_Height = 64;
    }
  hx = mul1616(scale, cs);
  hy = mul1616(scale, sn);
  c->ccb_HDX = hx * 16;
  c->ccb_HDY = hy * 16;
  c->ccb_VDX = -hy;
  c->ccb_VDY = hx;
  c->ccb_XPos = (Coord)((160 << 16) - 32 * (hx - hy));
  c->ccb_YPos = (Coord)((120 << 16) - 32 * (hy + hx));
}

static void gp_one(u32 code, u32 bits, s32 scale, s32 cs, s32 sn, u32 ace)
{
  CCB c;

  place_any(&c, code, bits, scale, cs, sn);
  if(ace != 0)
    c.ccb_Flags |= CCB_ACE;
  tk(&c, 8);
}

void group_genpath(void)
{
  static const s32 sc7[7] = { 21627, 32768, 46341, 55706, 65536, 92682, 131072 };
  static const s32 sc3[3] = { 21627, 39322, 55706 };
  static const s32 rt[2][2] = { { 56756, 32768 }, { 46341, 46341 } };
  static const s32 rs[2] = { 65536, 46341 };
  CCB c;
  u32 a;
  u32 i;
  u32 j;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('S', 5);
  progress(L("8 BITS CODES", "8 BITS CODED"));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 7; i++)
      gp_one(PRE0_BPP_8, 8, sc7[i], 65536, 0, a);
  progress(L("6 BITS CODES", "6 BITS CODED"));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 7; i++)
      gp_one(PRE0_BPP_6, 6, sc7[i], 65536, 0, a);
  progress("16 BITS");
  for(a = 0; a < 2; a++)
    for(i = 0; i < 3; i++)
      gp_one(PRE0_BPP_16, 16, sc3[i], 65536, 0, a);
  progress("ROTATIONS 8 BITS");
  for(a = 0; a < 2; a++)
    for(j = 0; j < 2; j++)
      for(i = 0; i < 2; i++)
        gp_one(PRE0_BPP_8, 8, rs[j], rt[i][0], rt[i][1], a);
  progress("PERSPECTIVE");
  for(a = 0; a < 2; a++)
    for(i = 0; i < 2; i++)
      {
        place_any(&c, PRE0_BPP_16, 16, 65536, 65536, 0);
        c.ccb_HDDX = -8192;
        if(i != 0)
          c.ccb_HDDY = 4096;
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, 8);
      }
  grid_end();
  sw_free();
}

/* ---------------------------------------------------------------- version 3.6 */

/* All the pages below measure with b_sync set by the menu: every draw starts right after a VBL. */

/* PHASE IMAGE, grid F, four values per row: one cel list drawn 0, 1, 2 .. 15 ms after the wake-up that follows a
   VBL; each value is the median of 5 draws.
    F0-F3  16 bits 320 x 48 (about 3 ms)
    F4-F7  16 bits 320 x 120 (about 8 ms)
    F8-FB  16 bits 320 x 200 (about 14 ms)
    FC-FF  the 6-bit packed picture of 2.3 VITESSE (about 11 ms)
   A draw that meets the work the system does after a VBL takes longer: the rows show where in the field and how
   much. Disc 3.5 measured draws back to back, so a draw of about half a field met that work one time in two. */
#define PH_REPS 5

static void phase_list(void)
{
  u32 d;
  u32 i;
  u32 t0;
  u32 tw;
  u32 e;
  u32 guard;

  for(d = 0; d < 16; d++)
    {
      stat_reset(&st);
      for(i = 0; i < PH_REPS; i++)
        {
          WaitVBL(b_vbl_ioreq, 1);
          tw = usec_now();
          guard = 0;
          do
            {
              e = usec_now() - tw;
              guard++;
            }
          while(e < d * 1000 && guard < 200000);
          t0 = usec_now();
          DrawCels(target, cc);
          stat_add(&st, usec_now() - t0);
        }
      sync_note(&st);
      grid_u(stat_median(&st));
    }
}

void group_phase(void)
{
  static const u16 ph[3] = { 48, 120, 200 };
  CCB c;
  u32 i;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('F', 4);
  for(i = 0; i < 3; i++)
    {
      progress(L("16 BITS 320 DE LARGE", "16 BITS 320 WIDE    "));
      make16(&c, SCREEN_W, ph[i], 0, 0);
      set_list(&c, 1);
      phase_list();
    }
  progress(L("IMAGE 6 BITS", "6-BIT IMAGE "));
  c = b_ccbs[C_IMAGE];
  set_list(&c, 1);
  phase_list();
  grid_end();
  sw_free();
}

/* CELS INCLINES, grid C, five values per row: 8 cels of 64 x 64.
    0-59  each row shifted from the row above by 0, 1/16, 1/8, 1/4, 1/2, 3/4, 1, 1.5, 2, 3 pixels (VDX, the middle row
          at x = 160): 16 bits (0-19), 8 bits coded (20-39), 6 bits coded (40-59); each shift without then with ACE
   60-73  16 bits centred in perspective: HDDX -16384 -8192 -4096 -2048 2048 4096 8192, without then with ACE
   74-79  16 bits, HDDX -8192 with HDDY 2048, 4096, 8192, without then with ACE
   80-83  8 bits coded, HDDX -8192 then 8192, without then with ACE
   On the FZ-10 (disc 3.4 J) a shift of up to 1 pixel costs the same as none; the core takes its general path. */
static void shear_src(CCB *c, u32 fmt)
{
  if(fmt == 0)
    make_holes(c, 0, 1);
  else if(fmt == 1)
    make_unpacked(c, PRE0_BPP_8, 8, 0);
  else
    make_holes(c, 0, 0);
  resize(c, 64, 64);
}

static void persp_one(u32 code, u32 bits, s32 hddx, s32 hddy)
{
  CCB c;
  u32 a;

  for(a = 0; a < 2; a++)
    {
      place_any(&c, code, bits, 65536, 65536, 0);
      c.ccb_HDDX = hddx;
      c.ccb_HDDY = hddy;
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
    }
}

void group_shear(void)
{
  static const s32 sh[10]    = { 0, 4096, 8192, 16384, 32768, 49152, 65536, 98304, 131072, 196608 };
  static const s32 hdx16[7]  = { -16384, -8192, -4096, -2048, 2048, 4096, 8192 };
  static const s32 hdy16[3]  = { 2048, 4096, 8192 };
  CCB c;
  CCB t;
  u32 f;
  u32 i;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('C', 5);
  for(f = 0; f < 3; f++)
    {
      progress((f == 0) ? "16 BITS" : ((f == 1) ? "8 BITS" : "6 BITS"));
      shear_src(&t, f);
      for(i = 0; i < 10; i++)
        for(a = 0; a < 2; a++)
          {
            c = t;
            c.ccb_XPos = (Coord)((128 << 16) - (sh[i] << 5));
            c.ccb_YPos = (Coord)(88 << 16);
            c.ccb_HDX  = H10;
            c.ccb_VDX  = sh[i];
            c.ccb_VDY  = V10;
            if(a != 0)
              c.ccb_Flags |= CCB_ACE;
            tk(&c, 8);
          }
    }
  progress("PERSPECTIVE");
  for(i = 0; i < 7; i++)
    persp_one(PRE0_BPP_16, 16, hdx16[i], 0);
  for(i = 0; i < 3; i++)
    persp_one(PRE0_BPP_16, 16, -8192, hdy16[i]);
  persp_one(PRE0_BPP_8, 8, -8192, 0);
  persp_one(PRE0_BPP_8, 8, 8192, 0);
  grid_end();
  sw_free();
}

/* CELS ROTATIONS, grid R, five values per row: 8 cels of 64 x 64 centred, turned by 0 2 5 10 20 30 45 60 90 degrees,
   each angle without then with ACE.
    0-53  8 bits coded at scale 0.71 (0-17), 1 (18-35), 1.41 (36-53)
   54-71  16 bits at scale 1 */
void group_rot(void)
{
  static const s32 rot[9][2] =
    {
      { 65536, 0 }, { 65496, 2287 }, { 65287, 5712 }, { 64540, 11380 }, { 61584, 22415 },
      { 56756, 32768 }, { 46341, 46341 }, { 32768, 56756 }, { 0, 65536 }
    };
  static const s32 rs[3] = { 46341, 65536, 92682 };
  u32 s;
  u32 i;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('R', 5);
  for(s = 0; s < 3; s++)
    {
      progress(L("8 BITS CODES", "8 BITS CODED"));
      for(i = 0; i < 9; i++)
        for(a = 0; a < 2; a++)
          gp_one(PRE0_BPP_8, 8, rs[s], rot[i][0], rot[i][1], a);
    }
  progress("16 BITS");
  for(i = 0; i < 9; i++)
    for(a = 0; a < 2; a++)
      gp_one(PRE0_BPP_16, 16, 65536, rot[i][0], rot[i][1], a);
  grid_end();
  sw_free();
}

/* CELS ACE ECHELLES, grid M, five values per row: 8 cels of 64 x 64 centred.
    0-32  scale 1 in X and 1.0 1.1 .. 2.0 in Y; for each Y: 16 bits with ACE, 8 bits coded with ACE, 16 bits without ACE
   33-57  16 bits with ACE, X 0.5 1 1.41 2 3 (outer loop) by Y 0.5 1 1.41 2 3 (inner loop)
   58-82  the same without ACE */
void group_acesc(void)
{
  static const s32 yf[11] =
    { 65536, 72090, 78643, 85197, 91750, 98304, 104858, 111411, 117965, 124518, 131072 };
  static const s32 xy[5] = { 32768, 65536, 92682, 131072, 196608 };
  CCB c;
  CCB t;
  u32 i;
  u32 j;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('M', 5);
  progress(L("Y DE 1 A 2", "Y 1 TO 2  "));
  make_unpacked(&t, PRE0_BPP_8, 8, 0);
  resize(&t, 64, 64);
  for(i = 0; i < 11; i++)
    {
      make16(&c, 64, 64, 0, 0);
      centre(&c, 65536, yf[i]);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
      c = t;
      centre(&c, 65536, yf[i]);
      c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
      make16(&c, 64, 64, 0, 0);
      centre(&c, 65536, yf[i]);
      tk(&c, 8);
    }
  progress(L("X PAR Y", "X BY Y "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 5; i++)
      for(j = 0; j < 5; j++)
        {
          make16(&c, 64, 64, 0, 0);
          centre(&c, xy[i], xy[j]);
          if(a == 0)
            c.ccb_Flags |= CCB_ACE;
          tk(&c, 8);
        }
  grid_end();
  sw_free();
}

/* CELS ACE ETROITS 2, grid T, five values per row, cels 240 rows high (several copies when small, k_for).
    0-39  16 bits with ACE, widths 8 9 10 12 16 17 20 24 32 33 (inner loop), the source starting 0, 1, 2, then 3 words
          after a multiple of 4 words (outer loop)
   40-49  16 bits without ACE, the same widths, the source at a multiple of 4 words
   50-65  8 bits coded with ACE, widths 16 20 24 32 36 40 48 52, the source at a multiple of 4 words, then 1 word after
   66-71  16 bits, rows contiguous in memory, widths 8 16 32, without then with ACE (outer loop)
   The core is 0.77-0.88 of the FZ-10 on narrow cels with ACE; a source refill threshold does not explain it. */
void group_narrow(void)
{
  static const u8 wn[10] = { 8, 9, 10, 12, 16, 17, 20, 24, 32, 33 };
  static const u8 w8[8]  = { 16, 20, 24, 32, 36, 40, 48, 52 };
  static const u8 wc[3]  = { 8, 16, 32 };
  CCB c;
  CCB t;
  u16 *base;
  u32 *g4;
  u32 o;
  u32 i;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  base = (u16 *)(((u32)b_src16 + 15) & ~(u32)15);
  grid_begin('T', 5);
  progress(L("16 BITS ACE", "16 BITS ACE"));
  for(o = 0; o < 4; o++)
    for(i = 0; i < 10; i++)
      {
        make16(&c, wn[i], SCREEN_H, 0, 0);
        c.ccb_SourcePtr = (CelData *)(base + o * 2);
        c.ccb_Flags |= CCB_ACE;
        tk(&c, k_for(wn[i], SCREEN_H));
      }
  progress(L("16 BITS SANS ACE", "16 BITS NO ACE  "));
  for(i = 0; i < 10; i++)
    {
      make16(&c, wn[i], SCREEN_H, 0, 0);
      c.ccb_SourcePtr = (CelData *)base;
      tk(&c, k_for(wn[i], SCREEN_H));
    }
  progress(L("8 BITS ACE", "8 BITS ACE"));
  make_unpacked(&t, PRE0_BPP_8, 8, 0);
  g4 = (u32 *)(((u32)gen + 15) & ~(u32)15);
  for(o = 0; o < 2; o++)
    for(i = 0; i < 8; i++)
      {
        c = t;
        resize(&c, w8[i], SCREEN_H);
        c.ccb_SourcePtr = (CelData *)(g4 + o);
        c.ccb_Flags |= CCB_ACE;
        tk(&c, k_for(w8[i], SCREEN_H));
      }
  progress(L("LIGNES CONTIGUES", "CONTIGUOUS ROWS "));
  for(a = 0; a < 2; a++)
    for(i = 0; i < 3; i++)
      {
        make16(&c, wc[i], SCREEN_H, 0, 0);
        c.ccb_PRE1 = ((wc[i] / 2 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
          PRE1_TLLSB_PDC0 | (wc[i] - PRE1_TLHPCNT_PREFETCH);
        if(a != 0)
          c.ccb_Flags |= CCB_ACE;
        tk(&c, k_for(wc[i], SCREEN_H));
      }
  grid_end();
  sw_free();
}

/* CELS FONDUS MELANGES, grid G, five values per row.
    0-27  the 1-pixel fill of 2.3 VITESSE stretched to 16x16, 32x32, 64x64, 128x128, 320x240, 320x8, 8x240 (outer loop),
          with PIXC 1F00 (solid), 1F81 (half blend), 1F80 (addition), 9F00 (the frame buffer pixel written back)
   28-41  16 bits, 8 cels of 64 x 64 centred, half blend at scale 0.5 0.71 1 1.41 2 2.5 3, without then with ACE
   42-45  8 bits coded, half blend at scale 1 then 2, without then with ACE
   46-49  16 bits, addition (1F80) at scale 1 then 2, without then with ACE
   Blended fills were 0.62-0.73 of the FZ-10 on the core (disc 3.1 E). */
static void blend_one(const CCB *t, s32 sc, u32 pixc)
{
  CCB c;
  u32 a;

  for(a = 0; a < 2; a++)
    {
      c = *t;
      centre(&c, sc, sc);
      c.ccb_PIXC = pixc;
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, 8);
    }
}

void group_fade(void)
{
  static const u16 fw[7] = { 16, 32, 64, 128, 320, 320, 8 };
  static const u16 fh[7] = { 16, 32, 64, 128, 240, 8, 240 };
  static const u32 px[4] = { 0x1F001F00, 0x1F811F81, 0x1F801F80, 0x9F009F00 };
  static const s32 bl[7] = { 32768, 46341, 65536, 92682, 131072, 163840, 196608 };
  CCB c;
  CCB t;
  u32 i;
  u32 j;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('G', 5);
  progress(L("APLATS", "FILLS "));
  for(i = 0; i < 7; i++)
    for(j = 0; j < 4; j++)
      {
        c = b_ccbs[C_FILL];
        c.ccb_HDX  = (s32)((u32)fw[i] << 20);
        c.ccb_VDY  = (s32)((u32)fh[i] << 16);
        c.ccb_PIXC = px[j];
        tk(&c, k_for(fw[i], fh[i]));
      }
  progress(L("MELANGES", "BLENDS  "));
  make16(&t, 64, 64, 0, 0);
  for(i = 0; i < 7; i++)
    blend_one(&t, bl[i], PIXC_HALF);
  make_unpacked(&t, PRE0_BPP_8, 8, 0);
  resize(&t, 64, 64);
  blend_one(&t, 65536, PIXC_HALF);
  blend_one(&t, 131072, PIXC_HALF);
  progress(L("ADDITIONS", "ADDITIONS"));
  make16(&t, 64, 64, 0, 0);
  blend_one(&t, 65536, 0x1F801F80);
  blend_one(&t, 131072, 0x1F801F80);
  grid_end();
  sw_free();
}

/* CELS HORS ECRAN, grid Z, five values per row: 2 copies of a 16-bit cel of 128 x 128, each place without then with ACE.
    0-53  scale 0.5 (0-17), 1 (18-35), 1.41 (36-53): centred, then out of the screen on the right by 1/4, 1/2, 3/4, all
          of it, then at the bottom by 1/4, 1/2, 3/4, all
   54-61  scale 1: out on the left by 1/2, all, then at the top by 1/2, all
   62-63  scale 1: far out on the right (x = 600)
   64-69  8 bits coded 128 x 128 at scale 1: centred, out on the right by 1/2, all
   Cels fully out on a side with ACE are 1.17-1.20 of the FZ-10 on the corrected core (disc 3.1 E). */
static void off_one(const CCB *t, s32 sc, s32 x, s32 y)
{
  CCB c;
  u32 a;

  for(a = 0; a < 2; a++)
    {
      c = *t;
      c.ccb_XPos = (Coord)(x * 65536);
      c.ccb_YPos = (Coord)(y * 65536);
      c.ccb_HDX  = sc * 16;
      c.ccb_VDY  = sc;
      if(a != 0)
        c.ccb_Flags |= CCB_ACE;
      tk(&c, 2);
    }
}

void group_off(void)
{
  static const s32 so[3] = { 32768, 65536, 92682 };
  CCB t;
  s32 sz;
  s32 q;
  u32 s;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('Z', 5);
  progress("16 BITS");
  make16(&t, 128, 128, 0, 0);
  for(s = 0; s < 3; s++)
    {
      sz = (128 * so[s]) >> 16;
      off_one(&t, so[s], 160 - sz / 2, 120 - sz / 2);
      for(q = 1; q <= 4; q++)
        off_one(&t, so[s], 320 - (sz * (4 - q)) / 4, 120 - sz / 2);
      for(q = 1; q <= 4; q++)
        off_one(&t, so[s], 160 - sz / 2, 240 - (sz * (4 - q)) / 4);
    }
  progress(L("GAUCHE ET HAUT", "LEFT AND TOP  "));
  off_one(&t, 65536, -64, 56);
  off_one(&t, 65536, -128, 56);
  off_one(&t, 65536, 96, -64);
  off_one(&t, 65536, 96, -128);
  off_one(&t, 65536, 600, 56);
  progress("8 BITS");
  make_unpacked(&t, PRE0_BPP_8, 8, 0);
  resize(&t, 128, 128);
  off_one(&t, 65536, 96, 56);
  off_one(&t, 65536, 256, 56);
  off_one(&t, 65536, 320, 56);
  grid_end();
  sw_free();
}

/* CELS TRANSPARENTS FINS, grid P, five values per row: the layouts of CELS TRANSPARENTS without holes, by 8, and all
   transparent (outer loop, inside the format), at scale 0.5 0.6 0.71 0.85 1.2 1.41 2 (from 1.2 up the cel is cut to
   fill the screen), each without then with ACE: 0-41 16 bits, 42-83 6 bits coded.
   The 6-bit transparent cels reduced are 1.3-1.7 of the FZ-10 on the core; 16 bits within 4 %. */
void group_trf(void)
{
  static const u8 lay[3] = { 0, 4, 10 };
  static const s32 sf[7] = { 32768, 39322, 46341, 55706, 78643, 92682, 131072 };
  CCB c;
  CCB t;
  u32 f;
  u32 k;
  u32 s;
  u32 a;

  if(sw_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  grid_begin('P', 5);
  for(f = 0; f < 2; f++)
    for(k = 0; k < 3; k++)
      {
        progress((f == 0) ? "16 BITS" : "6 BITS");
        make_holes(&t, lay[k], 1 - f);
        for(s = 0; s < 7; s++)
          for(a = 0; a < 2; a++)
            {
              c = t;
              if(sf[s] > 65536)
                resize(&c, (u32)(20971520 / sf[s]), (u32)(15728640 / sf[s]));
              c.ccb_HDX = sf[s] * 16;
              c.ccb_VDY = sf[s];
              if(a != 0)
                c.ccb_Flags |= CCB_ACE;
              tk(&c, 1);
            }
      }
  grid_end();
  sw_free();
}
