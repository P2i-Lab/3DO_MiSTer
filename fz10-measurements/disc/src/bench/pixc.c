/*
  P2i Lab 3DO bench 3.0, groups CELS PIXC and FORMATS GEOMETRIE: exact pixels, as
  16-bit hashes (hexadecimal), compared between machines.

  CELS PIXC: all 65536 values of the pixel processor control word, on small cels of
  random colours over a random background. One page with USEAV off, one with USEAV on.
  Each value's result goes into four tables: by its bits 15-10 (first source,
  multiplier source and factor), by bits 9-8, 7-6 and 0 (divider, second source,
  second divider), by AV bits 5-3 and by AV bits 2-1. A difference shows in the
  tables as the set of settings it belongs to.

  FORMATS GEOMETRIE: every source format against the options that change how a source
  pixel is turned into a colour, then random projected quadrilaterals.
*/

#include "bench.h"

#include "mem.h"
#include "string.h"

#define CELLS      256
#define FNV        16777619u
#define BG_W       320
#define BG_H       16
#define PATCH_BYTES (8 * 2 * 2)
#define FMT_SLOT   512
#define NFMT       10
#define GEO_GROUPS 32
#define GEO_QUADS  8

static CCB *pc;
static u16 *bg;
static u16 *patch;
static u8  *fsrc;
static u16 *fplut;
static u16 *p88;

static u32 tbl_a[64];
static u32 tbl_b[32];
static u32 tbl_c[8];
static u32 tbl_d[4];

static void px_free(void)
{
  if(pc != NULL)    FreeMem(pc, (CELLS + 2) * sizeof(CCB));
  if(bg != NULL)    FreeMem(bg, BG_W * BG_H * 2);
  if(patch != NULL) FreeMem(patch, PATCH_BYTES);
  if(fsrc != NULL)  FreeMem(fsrc, NFMT * FMT_SLOT);
  if(fplut != NULL) FreeMem(fplut, 64);
  if(p88 != NULL)   FreeMem(p88, 8 * 8 * 2);
  pc = NULL;
  bg = NULL;
  patch = NULL;
  fsrc = NULL;
  fplut = NULL;
  p88 = NULL;
}

static u32 hash16(u32 h)
{
  return (h ^ (h >> 16)) & 0xFFFF;
}

static int px_setup(void)
{
  u32 i;
  u32 seed;

  pc    = (CCB *)AllocMem((CELLS + 2) * sizeof(CCB), MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  bg    = (u16 *)AllocMem(BG_W * BG_H * 2, MEMTYPE_DRAM | MEMTYPE_CEL);
  patch = (u16 *)AllocMem(PATCH_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL);
  fsrc  = (u8 *)AllocMem(NFMT * FMT_SLOT, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  fplut = (u16 *)AllocMem(64, MEMTYPE_DRAM | MEMTYPE_CEL);
  p88   = (u16 *)AllocMem(8 * 8 * 2, MEMTYPE_DRAM | MEMTYPE_CEL);
  if(pc == NULL || bg == NULL || patch == NULL || fsrc == NULL || fplut == NULL || p88 == NULL)
    {
      px_free();
      return -1;
    }
  seed = 0x1234ABCD;
  for(i = 0; i < BG_W * BG_H; i++)
    bg[i] = (u16)(rand_next(&seed) >> 16);
  for(i = 0; i < 16; i++)
    patch[i] = (u16)(rand_next(&seed) >> 16);
  for(i = 0; i < 32; i++)
    fplut[i] = (u16)(rand_next(&seed) >> 16);
  for(i = 0; i < 64; i++)
    p88[i] = (u16)(rand_next(&seed) >> 16);
  return 0;
}

static u32 flags_full(void)
{
  return b_ccbs[C_16BPP].ccb_Flags;
}

/* a 16-bit uncoded cel of w x h pixels of data rows pitch_words apart */
static void cel16(CCB *c, void *src, u32 w, u32 h, u32 pitch_words)
{
  init_ccb(c, flags_full(), src, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((h - 1) << PRE0_VCNT_SHIFT),
           ((pitch_words - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (w - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  c->ccb_Width  = (s32)w;
  c->ccb_Height = (s32)h;
}

static u32 *screen_words(void)
{
  return (u32 *)b_sc.sc_Bitmaps[1]->bm_Buffer;
}

/* eight words starting at x in the pair of screen lines 2 * pair and 2 * pair + 1 */
static u32 hash_cell(const u32 *fb, u32 x, u32 pair)
{
  const u32 *w;
  u32 h;
  u32 i;

  w = fb + pair * SCREEN_W + x;
  h = 2166136261u;
  for(i = 0; i < 8; i++)
    h = (h ^ w[i]) * FNV;
  return h;
}

static void show_table(const u32 *t, u32 n)
{
  u32 i;

  for(i = 0; i < n; i++)
    grid_x(hash16(t[i]), 4);
}

void group_pixc(int page)
{
  Item bm;
  u32 *fb;
  u32 i;
  u32 hi;
  u32 v;
  u32 h;
  u32 x;
  u32 y;
  CCB *back;

  if(px_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  for(i = 0; i < 64; i++) tbl_a[i] = 2166136261u;
  for(i = 0; i < 32; i++) tbl_b[i] = 2166136261u;
  for(i = 0; i < 8; i++)  tbl_c[i] = 2166136261u;
  for(i = 0; i < 4; i++)  tbl_d[i] = 2166136261u;

  back = &pc[CELLS];
  cel16(back, bg, BG_W, BG_H, BG_W * 2 / 4);
  for(i = 0; i < CELLS; i++)
    {
      cel16(&pc[i], patch, 8, 2, 4);
      pc[i].ccb_Flags &= ~CCB_LAST;
      if(page != 0)
        pc[i].ccb_Flags |= CCB_USEAV;
      pc[i].ccb_NextPtr = &pc[i + 1];
      pc[i].ccb_XPos = (Coord)((i & 31) * 8 * 65536);
      pc[i].ccb_YPos = (Coord)((i >> 5) * 2 * 65536);
    }
  pc[CELLS - 1].ccb_Flags  |= CCB_LAST;
  pc[CELLS - 1].ccb_NextPtr = NULL;

  for(hi = 0; hi < 256; hi++)
    {
      if((hi & 15) == 0)
        progress("PIXC");
      DrawCels(bm, back);
      for(i = 0; i < CELLS; i++)
        pc[i].ccb_PIXC = ((hi << 8) | i) * 0x00010001;
      DrawCels(bm, pc);
      for(i = 0; i < CELLS; i++)
        {
          v = (hi << 8) | i;
          x = (i & 31) * 8;
          y = i >> 5;
          h = hash_cell(fb, x, y);
          tbl_a[v >> 10] = (tbl_a[v >> 10] ^ h) * FNV;
          h = (h ^ (h >> 7)) * FNV;
          tbl_b[(((v >> 8) & 3) << 3) | (((v >> 6) & 3) << 1) | (v & 1)] =
            (tbl_b[(((v >> 8) & 3) << 3) | (((v >> 6) & 3) << 1) | (v & 1)] ^ h) * FNV;
          tbl_c[(v >> 3) & 7] = (tbl_c[(v >> 3) & 7] ^ h) * FNV;
          tbl_d[(v >> 1) & 3] = (tbl_d[(v >> 1) & 3] ^ h) * FNV;
        }
    }

  grid_begin((page == 0) ? 'P' : 'Q', 7);
  show_table(tbl_a, 64);
  grid_end();
  grid_begin('R', 7);
  show_table(tbl_b, 32);
  grid_end();
  grid_begin('S', 7);
  show_table(tbl_c, 8);
  show_table(tbl_d, 4);
  grid_end();
  px_free();
}

typedef struct Fmt
{
  u32 code;
  u32 bits;
  u32 linear;
  u32 packed;
} Fmt;

static const Fmt fmts[NFMT] =
  {
    { PRE0_BPP_1,  1,  0, 0 },
    { PRE0_BPP_2,  2,  0, 0 },
    { PRE0_BPP_4,  4,  0, 0 },
    { PRE0_BPP_6,  6,  0, 0 },
    { PRE0_BPP_8,  8,  0, 0 },
    { PRE0_BPP_16, 16, 0, 0 },
    { PRE0_BPP_8,  8,  PRE0_LINEAR, 0 },
    { PRE0_BPP_16, 16, PRE0_LINEAR, 0 },
    { PRE0_BPP_6,  6,  0, CCB_PACKED },
    { PRE0_BPP_16, 16, PRE0_LINEAR, CCB_PACKED }
  };

/* 16 x 8 pixels of random data in format f, in its slot; returns the row spacing in words */
static u32 build_format(const Fmt *f, u32 *buf, u32 *seed)
{
  u32 x;
  u32 y;
  u32 pos;
  u32 row;
  u32 words;
  u32 wpr;
  u32 mask;

  mask = (1u << f->bits) - 1;
  if(f->bits == 16)
    mask = 0xFFFF;
  memset(buf, 0, FMT_SLOT);
  if(f->packed == 0)
    {
      wpr = (16 * f->bits + 31) / 32;
      if(wpr < 2)
        wpr = 2;
      for(y = 0; y < 8; y++)
        {
          pos = y * wpr * 32;
          for(x = 0; x < 16; x++)
            put_bits(buf, &pos, (rand_next(seed) >> 12) & mask, f->bits);
        }
      return wpr;
    }
  row = 0;
  for(y = 0; y < 8; y++)
    {
      pos = row * 32;
      put_bits(buf, &pos, 0, (f->bits >= 8) ? 16 : 8);
      put_bits(buf, &pos, PACK_LITERAL, 2);
      put_bits(buf, &pos, 15, 6);
      for(x = 0; x < 16; x++)
        put_bits(buf, &pos, (rand_next(seed) >> 12) & mask, f->bits);
      put_bits(buf, &pos, PACK_EOL, 2);
      words = ((pos + 31) >> 5) - row;
      if(words < 2)
        words = 2;
      buf[row] |= (words - 2) << ((f->bits >= 8) ? 16 : 24);
      row += words;
    }
  return 0;
}

static void formats(void)
{
  static u32 ga[NFMT * 4];
  static u32 gb[24];
  static u32 gc[8];
  Item bm;
  u32 *fb;
  CCB *cel;
  CCB *back;
  u32 seed;
  u32 f;
  u32 wpr;
  u32 tl;
  u32 bn;
  u32 px;
  u32 po;
  u32 pa;
  u32 pt;
  u32 ma;
  u32 i;
  u32 j;
  u32 h;
  u32 flags;
  u32 base;
  const Fmt *m;
  u32 *buf;
  const u32 *w;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  cel = &pc[0];
  back = &pc[1];
  cel16(back, bg, 16, 8, BG_W * 2 / 4);
  seed = 0x77AA1100;
  for(i = 0; i < NFMT * 4; i++) ga[i] = 2166136261u;
  for(i = 0; i < 24; i++)       gb[i] = 2166136261u;
  for(i = 0; i < 8; i++)        gc[i] = 2166136261u;
  base = (b_ccbs[C_16BPP].ccb_Flags | CCB_LDPLUT) & ~(CCB_BGND | CCB_NOBLK);

  for(f = 0; f < NFMT; f++)
    {
      progress("FORMATS");
      m = &fmts[f];
      buf = (u32 *)(fsrc + f * FMT_SLOT);
      wpr = build_format(m, buf, &seed);
      for(tl = 0; tl < 4; tl++)
        for(bn = 0; bn < 4; bn++)
          for(px = 0; px < 2; px++)
            for(po = 0; po < 3; po++)
              for(pa = 0; pa < 2; pa++)
                for(pt = 0; pt < 2; pt++)
                  for(ma = 0; ma < 2; ma++)
                    {
                      flags = base | m->packed;
                      if(bn & 1) flags |= CCB_BGND;
                      if(bn & 2) flags |= CCB_NOBLK;
                      if(px)     flags |= CCB_PXOR;
                      if(po == 1) flags |= PMODE_ZERO;
                      if(po == 2) flags |= PMODE_ONE;
                      if(pt)     flags |= 5;
                      if(ma)     flags |= CCB_MARIA;
                      init_ccb(cel, flags, buf, fplut,
                               m->code | m->linear | (7u << PRE0_VCNT_SHIFT),
                               ((m->packed != 0) ? 0 :
                                ((wpr - PRE1_WOFFSET_PREFETCH) << ((m->bits >= 8) ? PRE1_WOFFSET10_SHIFT : PRE1_WOFFSET8_SHIFT))) |
                               (tl << PRE1_TLLSB_SHIFT) |
                               ((m->packed != 0) ? 0 : (16 - PRE1_TLHPCNT_PREFETCH)),
                               pa ? PIXC_HALF : PIXC_SOLID);
                      cel->ccb_Width = 16;
                      cel->ccb_Height = 8;
                      DrawCels(bm, back);
                      DrawCels(bm, cel);
                      h = 2166136261u;
                      for(i = 0; i < 4; i++)
                        {
                          w = fb + i * SCREEN_W;
                          for(j = 0; j < 16; j++)
                            h = (h ^ w[j]) * FNV;
                        }
                      ga[f * 4 + tl] = (ga[f * 4 + tl] ^ h) * FNV;
                      j = bn * 6 + px * 3 + po;
                      gb[j] = (gb[j] ^ h) * FNV;
                      j = pa * 4 + pt * 2 + ma;
                      gc[j] = (gc[j] ^ h) * FNV;
                    }
    }
  grid_begin('F', 7);
  show_table(ga, NFMT * 4);
  grid_end();
  grid_begin('G', 7);
  show_table(gb, 24);
  show_table(gc, 8);
  grid_end();
}

/* random position, size, shear, rotation and perspective on an 8 x 8 source */
static void geometry(void)
{
  static u32 g[GEO_GROUPS];
  Item bm;
  u32 *fb;
  u32 seed;
  u32 i;
  u32 q;
  u32 h;
  CCB *c;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  seed = 0xBEEF0123;
  for(i = 0; i < GEO_GROUPS; i++)
    {
      progress(L("GEOMETRIE", " GEOMETRY"));
      for(q = 0; q < GEO_QUADS; q++)
        {
          c = &pc[q];
          cel16(c, p88, 8, 8, 4);
          c->ccb_Flags &= ~CCB_LAST;
          c->ccb_NextPtr = &pc[q + 1];
          c->ccb_XPos = (Coord)(rand_next(&seed) % (SCREEN_W * 65536));
          c->ccb_YPos = (Coord)(rand_next(&seed) % (SCREEN_H * 65536));
          c->ccb_HDX  = (s32)(rand_next(&seed) % (8u << 20)) - (4 << 20);
          c->ccb_HDY  = (s32)(rand_next(&seed) % (8u << 20)) - (4 << 20);
          c->ccb_VDX  = (s32)(rand_next(&seed) % (8u << 16)) - (4 << 16);
          c->ccb_VDY  = (s32)(rand_next(&seed) % (8u << 16)) - (4 << 16);
          c->ccb_HDDX = (s32)(rand_next(&seed) % (1u << 18)) - (1 << 17);
          c->ccb_HDDY = (s32)(rand_next(&seed) % (1u << 18)) - (1 << 17);
        }
      pc[GEO_QUADS - 1].ccb_Flags  |= CCB_LAST;
      pc[GEO_QUADS - 1].ccb_NextPtr = NULL;
      DrawCels(bm, &b_ccbs[C_FILL]);
      DrawCels(bm, pc);
      h = 2166136261u;
      for(q = 0; q < SCREEN_W * SCREEN_H / 2; q++)
        h = (h ^ fb[q]) * FNV;
      g[i] = h;
    }
  grid_begin('H', 7);
  show_table(g, GEO_GROUPS);
  grid_end();
}

void group_formats(void)
{
  if(px_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  formats();
  geometry();
  px_free();
}

/* ---------------------------------------------------------------- version 3.1 */

/* PIXELS EN CLAIR: rows of 32 pixels whose results are printed one character each
   (0-9 then A-V for the values 0 to 31), for the settings where the hashes of 3.0 could
   not say what the console does. */

static const char b32[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

static void row_bg(u32 *fb, u32 bg)
{
  u32 x;

  for(x = 0; x < 32; x++)
    fb[x] = (bg << 16) | bg;
}

static void set_ramp(u32 channel, u32 pbit)
{
  u16 *s;
  u32 i;

  s = (u16 *)fsrc;
  for(i = 0; i < 32; i++)
    s[i] = (u16)((pbit << 15) | (i << (10 - channel * 5)));
}

static void prep_ramp(u32 pixc, u32 extra)
{
  cel16(&pc[0], fsrc, 32, 1, 16);
  pc[0].ccb_Flags |= CCB_BGND | CCB_NOBLK | extra;
  pc[0].ccb_PIXC = pixc * 0x00010001;
}

static void out_row(const char *tag, const u32 *fb, u32 shift)
{
  char *p;
  u32 x;

  p = new_line(tag);
  p = put_s(p, " ");
  for(x = 0; x < 32; x++)
    *p++ = b32[(fb[x] >> (16 + shift)) & 31];
  *p = 0;
  log_line();
}

static void ramp_three(Item bm, u32 *fb, const char *tags, u32 channel, u32 pbit, u32 pixc, u32 bg)
{
  static char tag[3];
  static const u32 shifts[3] = { 10, 5, 0 };
  u32 k;

  set_ramp(channel, pbit);
  prep_ramp(pixc, 0);
  row_bg(fb, bg);
  DrawCels(bm, &pc[0]);
  for(k = 0; k < 3; k++)
    {
      tag[0] = tags[0];
      tag[1] = "RGB"[k];
      tag[2] = 0;
      out_row(tag, fb, shifts[k]);
    }
}

/* the multiplier taken from the pixel (MS = PIN) or per colour (MS = PDC): the frame
   buffer is 16, 16, 16 and is divided by 8, so a result of 2 * m means a factor m */
static void ms_page(u32 ms)
{
  Item bm;
  u32 *fb;
  u32 grey;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  grey = MakeRGB15(16, 16, 16);
  ramp_three(bm, fb, "R", 0, 0, PPMPC_1S_CFBD | ms | PPMPC_SF_8, grey);
  ramp_three(bm, fb, "G", 1, 0, PPMPC_1S_CFBD | ms | PPMPC_SF_8, grey);
  ramp_three(bm, fb, "B", 2, 0, PPMPC_1S_CFBD | ms | PPMPC_SF_8, grey);
  ramp_three(bm, fb, "P", 0, 1, PPMPC_1S_CFBD | ms | PPMPC_SF_8, grey);
  ramp_three(bm, fb, "S", 0, 0, ms | PPMPC_SF_8, grey);
}

/* the options of the adder with USEAV: pixel (ramp of red) + frame buffer */
static void av_page(void)
{
  static const u8 av[17] =
    {
      0x00, 0x10, 0x20, 0x30, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x12,
      0x00, 0x02, 0x04, 0x08, 0x0A
    };
  static char tag[3];
  Item bm;
  u32 *fb;
  u32 i;
  u32 v;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  set_ramp(0, 0);
  for(i = 0; i < 17; i++)
    {
      v = (i < 12) ? 24 : 8;
      prep_ramp(PPMPC_MF_8 | PPMPC_SF_8 | PPMPC_2S_CFBD | av[i], CCB_USEAV);
      row_bg(fb, MakeRGB15(v, v, v));
      DrawCels(bm, &pc[0]);
      tag[0] = 'E';
      tag[1] = "0123456789ABCDEFG"[i];
      tag[2] = 0;
      out_row(tag, fb, 10);
    }
}

static void px_hex(char **pp, u32 v)
{
  *pp = put_s(*pp, " ");
  *pp = put_x(*pp, v, 4);
}

/* 8-bit uncoded with the four TLLSB settings, then 1-bit and 2-bit coded pixels */
static void fmt_page(void)
{
  static char tag[3];
  Item bm;
  u32 *fb;
  CCB *c;
  u8 *s;
  u32 *w;
  u32 tl;
  u32 i;
  char *p;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  c = &pc[0];
  s = fsrc;
  w = (u32 *)fsrc;
  memset(fsrc, 0, 64);
  for(i = 0; i < 8; i++)
    {
      s[i]     = (u8)(i << 5);
      s[8 + i] = (u8)(i << 2);
    }
  for(i = 0; i < 4; i++)
    s[16 + i] = (u8)i;
  s[20] = 0xFF;
  s[21] = 0x55;
  s[22] = 0xAA;
  s[23] = 0x92;
  for(tl = 0; tl < 4; tl++)
    {
      init_ccb(c, flags_full() | CCB_BGND | CCB_NOBLK, fsrc, NULL,
               PRE0_LINEAR | PRE0_BPP_8,
               ((6 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
               (tl << PRE1_TLLSB_SHIFT) | (24 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
      c->ccb_Width  = 24;
      c->ccb_Height = 1;
      row_bg(fb, 0);
      DrawCels(bm, c);
      tag[0] = 'T';
      tag[1] = (char)('0' + tl);
      tag[2] = 0;
      p = new_line(tag);
      p = put_s(p, " ");
      for(i = 0; i < 8; i++)
        *p++ = b32[(fb[i] >> 26) & 31];
      *p++ = ' ';
      for(i = 8; i < 16; i++)
        *p++ = b32[(fb[i] >> 21) & 31];
      *p++ = ' ';
      for(i = 16; i < 20; i++)
        *p++ = b32[(fb[i] >> 16) & 31];
      *p = 0;
      log_line();
      tag[0] = 'M';
      p = new_line(tag);
      for(i = 20; i < 24; i++)
        px_hex(&p, (fb[i] >> 16) & 0x7FFF);
      (void)p;
      log_line();
    }

  /* 1 bit per pixel: 0 1 0 1, then 2 bits per pixel: 0 1 2 3 */
  for(tl = 0; tl < 2; tl++)
    {
      memset(fsrc, 0, 16);
      w[0] = (tl == 0) ? 0x50000000u : 0x1B000000u;
      init_ccb(c, flags_full() | CCB_LDPLUT | CCB_BGND | CCB_NOBLK, fsrc, fplut,
               (tl == 0) ? PRE0_BPP_1 : PRE0_BPP_2,
               ((2 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET8_SHIFT) |
               PRE1_TLLSB_PDC0 | (4 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
      c->ccb_Width  = 4;
      c->ccb_Height = 1;
      row_bg(fb, 0);
      DrawCels(bm, c);
      p = new_line((tl == 0) ? "C1" : "C2");
      for(i = 0; i < 4; i++)
        px_hex(&p, (fb[i] >> 16) & 0x7FFF);
      (void)p;
      log_line();
    }
  p = new_line("PL");
  for(i = 0; i < 4; i++)
    px_hex(&p, fplut[i] & 0x7FFF);
  (void)p;
  log_line();
}

/* version 3.2: what AV bits 4:3 = 3 do (the term from the frame buffer, read with a source of 0 over a ramp of the
   frame buffer, and the term from the source with a frame buffer of 0), and TLLSB on 16-bit pixels */
static void pix5_page(void)
{
  static const u32 tl[4] = { PRE1_TLLSB_0, PRE1_TLLSB_PDC0, PRE1_TLLSB_PDC4, PRE1_TLLSB_PDC5 };
  static char tag[3];
  Item bm;
  u32 *fb;
  u16 *s;
  char *p;
  u32 x;
  u32 t;

  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  s = (u16 *)fsrc;

  for(t = 0; t < 2; t++)
    {
      for(x = 0; x < 32; x++)
        {
          s[x] = (u16)((t == 0) ? 0 : 3 << 10);
          fb[x] = ((u32)MakeRGB15(x, x, x) << 16) | MakeRGB15(x, x, x);
        }
      prep_ramp(PPMPC_MF_8 | PPMPC_SF_8 | PPMPC_2S_CFBD | 0x30, CCB_USEAV);
      DrawCels(bm, &pc[0]);
      out_row((t == 0) ? "Z0" : "Z1", fb, 10);
    }
  set_ramp(0, 0);
  prep_ramp(PPMPC_MF_8 | PPMPC_SF_8 | PPMPC_2S_CFBD | 0x30, CCB_USEAV);
  row_bg(fb, 0);
  DrawCels(bm, &pc[0]);
  out_row("Z2", fb, 10);
  prep_ramp(PPMPC_MF_4 | PPMPC_SF_8 | PPMPC_2S_CFBD | 0x30, CCB_USEAV);
  row_bg(fb, 0);
  DrawCels(bm, &pc[0]);
  out_row("Z3", fb, 10);
  for(x = 0; x < 32; x++)
    {
      s[x] = 0;
      fb[x] = ((u32)MakeRGB15(x, x, x) << 16) | MakeRGB15(x, x, x);
    }
  prep_ramp(PPMPC_MF_8 | PPMPC_SF_4 | PPMPC_2S_CFBD | 0x30, CCB_USEAV);
  DrawCels(bm, &pc[0]);
  out_row("Z4", fb, 10);

  /* blue = x, bit 0 of green = the inverse of bit 0 of x */
  for(t = 0; t < 4; t++)
    {
      for(x = 0; x < 32; x++)
        s[x] = (u16)(x | (((~x) & 1) << 5));
      prep_ramp(PIXC_SOLID >> 16, 0);
      pc[0].ccb_PRE1 = (pc[0].ccb_PRE1 & ~PRE1_TLLSB_MASK) | tl[t];
      row_bg(fb, 0);
      DrawCels(bm, &pc[0]);
      tag[0] = 'U';
      tag[1] = (char)('0' + t);
      tag[2] = 0;
      out_row(tag, fb, 0);
    }

  /* one 16-bit pixel holding only blue bit 0, without BGND over a white frame buffer: is it transparent? */
  p = new_line("TR");
  for(t = 0; t < 4; t++)
    {
      s[0] = 1;
      cel16(&pc[0], fsrc, 1, 1, 16);
      pc[0].ccb_Flags |= CCB_NOBLK;
      pc[0].ccb_Flags &= ~CCB_BGND;
      pc[0].ccb_PRE1 = (pc[0].ccb_PRE1 & ~PRE1_TLLSB_MASK) | tl[t];
      row_bg(fb, 0x7FFF);
      DrawCels(bm, &pc[0]);
      px_hex(&p, (fb[0] >> 16) & 0x7FFF);
    }
  (void)p;
  log_line();
}

void group_pix3(int page)
{
  if(px_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  if(page == 0)
    ms_page(PPMPC_MS_PIN);
  else if(page == 1)
    ms_page(PPMPC_MS_PDC);
  else if(page == 2)
    av_page();
  else if(page == 3)
    fmt_page();
  else
    pix5_page();
  px_free();
}

/* ---------------------------------------------------------------- version 3.5 */

/* RENDU DETAIL 1/2 (grid D): the formats whose FORMATS hashes still differ on the core (1-bit and 2-bit coded, 8-bit uncoded), with
   the same data (same seed, every format built in turn) and the same 768 draws as FORMATS, hashed for each format alone:
    0-11  by TLLSB 0-3 (1 bit, 2 bits, 8 bits uncoded)
   12-83  by BGND/NOBLK x PXOR x POVER (24 for each format, in the order of grid G)
   84-107 by PIXC x PLUTA x MARIA (8 for each format, in the order of grid G) */
void formats_detail(void)
{
  static const u8 which[3] = { 0, 1, 6 };
  static u32 da[3 * 4];
  static u32 db[3 * 24];
  static u32 dc[3 * 8];
  u32 wpr[NFMT];
  Item bm;
  u32 *fb;
  CCB *cel;
  CCB *back;
  u32 seed;
  u32 f;
  u32 k;
  u32 tl;
  u32 bn;
  u32 px;
  u32 po;
  u32 pa;
  u32 pt;
  u32 ma;
  u32 i;
  u32 j;
  u32 h;
  u32 flags;
  u32 base;
  const Fmt *m;
  u32 *buf;
  const u32 *w;

  if(px_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  cel = &pc[0];
  back = &pc[1];
  cel16(back, bg, 16, 8, BG_W * 2 / 4);
  seed = 0x77AA1100;
  for(f = 0; f < NFMT; f++)
    wpr[f] = build_format(&fmts[f], (u32 *)(fsrc + f * FMT_SLOT), &seed);
  for(i = 0; i < 3 * 4; i++)  da[i] = 2166136261u;
  for(i = 0; i < 3 * 24; i++) db[i] = 2166136261u;
  for(i = 0; i < 3 * 8; i++)  dc[i] = 2166136261u;
  base = (b_ccbs[C_16BPP].ccb_Flags | CCB_LDPLUT) & ~(CCB_BGND | CCB_NOBLK);
  for(k = 0; k < 3; k++)
    {
      progress("FORMATS DETAIL");
      f = which[k];
      m = &fmts[f];
      buf = (u32 *)(fsrc + f * FMT_SLOT);
      for(tl = 0; tl < 4; tl++)
        for(bn = 0; bn < 4; bn++)
          for(px = 0; px < 2; px++)
            for(po = 0; po < 3; po++)
              for(pa = 0; pa < 2; pa++)
                for(pt = 0; pt < 2; pt++)
                  for(ma = 0; ma < 2; ma++)
                    {
                      flags = base | m->packed;
                      if(bn & 1) flags |= CCB_BGND;
                      if(bn & 2) flags |= CCB_NOBLK;
                      if(px)     flags |= CCB_PXOR;
                      if(po == 1) flags |= PMODE_ZERO;
                      if(po == 2) flags |= PMODE_ONE;
                      if(pt)     flags |= 5;
                      if(ma)     flags |= CCB_MARIA;
                      init_ccb(cel, flags, buf, fplut,
                               m->code | m->linear | (7u << PRE0_VCNT_SHIFT),
                               ((m->packed != 0) ? 0 :
                                ((wpr[f] - PRE1_WOFFSET_PREFETCH) << ((m->bits >= 8) ? PRE1_WOFFSET10_SHIFT : PRE1_WOFFSET8_SHIFT))) |
                               (tl << PRE1_TLLSB_SHIFT) |
                               ((m->packed != 0) ? 0 : (16 - PRE1_TLHPCNT_PREFETCH)),
                               pa ? PIXC_HALF : PIXC_SOLID);
                      cel->ccb_Width = 16;
                      cel->ccb_Height = 8;
                      DrawCels(bm, back);
                      DrawCels(bm, cel);
                      h = 2166136261u;
                      for(i = 0; i < 4; i++)
                        {
                          w = fb + i * SCREEN_W;
                          for(j = 0; j < 16; j++)
                            h = (h ^ w[j]) * FNV;
                        }
                      da[k * 4 + tl] = (da[k * 4 + tl] ^ h) * FNV;
                      j = bn * 6 + px * 3 + po;
                      db[k * 24 + j] = (db[k * 24 + j] ^ h) * FNV;
                      j = pa * 4 + pt * 2 + ma;
                      dc[k * 8 + j] = (dc[k * 8 + j] ^ h) * FNV;
                    }
    }
  grid_begin('D', 7);
  show_table(da, 3 * 4);
  show_table(db, 3 * 24);
  show_table(dc, 3 * 8);
  grid_end();
  px_free();
}

/* RENDU DETAIL 2/2, grid P: group 12 of the random quads of FORMATS (the only group whose hash differs on the core), the hash of
   the whole screen after each of its 8 quads, drawn over Strahl's fill as in FORMATS */
void geometry_detail(void)
{
  Item bm;
  u32 *fb;
  u32 seed;
  u32 g;
  u32 i;
  u32 q;
  u32 k;
  u32 h;
  u32 fl;
  CCB *nx;
  CCB *c;

  if(px_setup() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  bm = b_sc.sc_BitmapItems[1];
  fb = screen_words();
  seed = 0xBEEF0123;
  for(g = 0; g <= 12; g++)
    {
      for(q = 0; q < GEO_QUADS; q++)
        {
          c = &pc[q];
          cel16(c, p88, 8, 8, 4);
          c->ccb_Flags &= ~CCB_LAST;
          c->ccb_NextPtr = &pc[q + 1];
          c->ccb_XPos = (Coord)(rand_next(&seed) % (SCREEN_W * 65536));
          c->ccb_YPos = (Coord)(rand_next(&seed) % (SCREEN_H * 65536));
          c->ccb_HDX  = (s32)(rand_next(&seed) % (8u << 20)) - (4 << 20);
          c->ccb_HDY  = (s32)(rand_next(&seed) % (8u << 20)) - (4 << 20);
          c->ccb_VDX  = (s32)(rand_next(&seed) % (8u << 16)) - (4 << 16);
          c->ccb_VDY  = (s32)(rand_next(&seed) % (8u << 16)) - (4 << 16);
          c->ccb_HDDX = (s32)(rand_next(&seed) % (1u << 18)) - (1 << 17);
          c->ccb_HDDY = (s32)(rand_next(&seed) % (1u << 18)) - (1 << 17);
        }
      pc[GEO_QUADS - 1].ccb_Flags  |= CCB_LAST;
      pc[GEO_QUADS - 1].ccb_NextPtr = NULL;
    }
  progress(L("GEOMETRIE 12", "GEOMETRY 12 "));
  grid_begin('P', 7);
  for(k = 1; k <= GEO_QUADS; k++)
    {
      fl = pc[k - 1].ccb_Flags;
      nx = pc[k - 1].ccb_NextPtr;
      pc[k - 1].ccb_Flags |= CCB_LAST;
      pc[k - 1].ccb_NextPtr = NULL;
      DrawCels(bm, &b_ccbs[C_FILL]);
      DrawCels(bm, pc);
      pc[k - 1].ccb_Flags = fl;
      pc[k - 1].ccb_NextPtr = nx;
      h = 2166136261u;
      for(i = 0; i < SCREEN_W * SCREEN_H / 2; i++)
        h = (h ^ fb[i]) * FNV;
      grid_x(hash16(h), 4);
    }
  grid_end();
  px_free();
}
