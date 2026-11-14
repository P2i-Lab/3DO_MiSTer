/*
  P2i Lab 3DO bench 3.1, group SCENE MAGICIEN: the cel lists Strahl (USA) draws on its
  animated wizard screen, rebuilt with the same formats, sizes, scales, blends and packet
  mix (read from memory dumps in Opera, frames 8400 and 9901). The pixels are generated
  here. On MiSTer that screen ends too early and cuts the dialogue; this page says what the
  same lists cost on the console, and which cel is responsible when the core differs.

  Each number of the grid is the median time, in microseconds, of one DrawCels:

  W0  LISTE A      FOND A        7 AJOUTS       MAGE
  W1  the same four with ACE set on every cel
  W2  FOND IN DRAM  FOND NO ROTATION  FOND SCALE 1  FOND UNPACKED
  W3  FOND NO USEAV  7 AJOUTS SOLID  7 AJOUTS NO ZERO  7 AJOUTS SCALE 1
  W4  LISTE B      FOND B        3 MEDAILLONS   LISTE B ACE
  W5  LOOP A FIELDS  LOOP A US   LOOP B FIELDS  LOOP B US   (60 x DrawCels + WaitVBL 1)
  CRC of the frame buffer after list A and after list B, drawn once on a black screen.
*/

#include "bench.h"

#include "mem.h"
#include "string.h"

#define REPS       9
#define LOOPS      60
#define LIST_MAX   10

/* FOND: 300 x 300, 16 bits, packed: 5 literal packets per row, 152 words per row */
#define BG_SIZE    300
#define BG_WORDS   152
#define BG_BYTES   (BG_SIZE * BG_WORDS * 4)
#define BGU_BYTES  (BG_SIZE * BG_SIZE * 2)
#define BG_FLAGS   0x3F660600
#define BG_PRE0    0x00004AD6
#define BG_PRE1    0x0094112B

/* AJOUTS: 60 x 60, 16 bits, added to the frame buffer, 57 percent of zero pixels */
#define ADD_SIZE   60
#define ADD_BYTES  (ADD_SIZE * ADD_SIZE * 2)
#define ADD_R2     497
#define ADD_FLAGS  0x3F670000
#define ADD_PRE0   0x00000ED6
#define ADD_PRE1   0x001C003B
#define ADD_PIXC   0x1F001F80

/* MEDAILLONS: 50 x 70, 8 bits uncoded, added, 32 percent of zero pixels */
#define MED_W      50
#define MED_H      70
#define MED_WORDS  13
#define MED_BYTES  (MED_H * MED_WORDS * 4)
#define MED_PRE0   0x00001155
#define MED_PRE1   0x000B0031

/* MAGE: 160 x 160, 6 bits coded, packed */
#define WIZ_SIZE   160
#define WIZ_ROOM   32
#define WIZ_BYTES  (WIZ_SIZE * WIZ_ROOM * 4)
#define WIZ_FLAGS  0x3FE60200
#define WIZ_PRE0   0x000027C4
#define WIZ_PRE1   0x1C00109F
#define WIZ_X      0x00500000
#define WIZ_Y      0x001E0000

#define CC_BYTES   (LIST_MAX * sizeof(CCB))
#define S(x)       ((s32)(x))

/* x, y, hdx, hdy, vdx, vdy */
static const s32 bg_a[6] =
  { S(0x015B8000), S(0xFF8D8000), S(0xFFE97E4C), S(0x000281B4), S(0x0000281B), S(0x0001681B) };
static const s32 bg_b[6] =
  { S(0x01628000), S(0x015C8000), S(0x0001B333), S(0xFFEAD99A), S(0xFFFEAD9A), S(0xFFFFE4CD) };
static const s32 bg_flat[6] =
  { -52 * 65536, -92 * 65536, 0x0016A09E, 0, 0, 0x00016A09 };
static const s32 bg_one[6] =
  { 10 * 65536, -30 * 65536, 1 << 20, 0, 0, 1 << 16 };

/* x, y, hdx, vdy; the first four come before the wizard in list A */
static const s32 add_a[7][4] =
  {
    { 0x00368000, 0x006E8000, 0x00048888, 0x00004444 },
    { 0x00718000, 0x005A8000, 0x0001DDDD, 0x00000CCC },
    { 0x00C98000, 0x005A8000, 0x0001DDDD, 0x00001111 },
    { 0x00F88000, 0x006E8000, 0x0004CCCC, 0x00004888 },
    { 0x00DE8000, 0x00898000, 0x00080000, 0x00008888 },
    { 0x008E8000, 0x00948000, 0x00095555, 0x0000A666 },
    { 0x00428000, 0x00888000, 0x00080000, 0x00008888 }
  };

/* the same positions at full size */
static const s32 add_one[7][4] =
  {
    {  10 * 65536,  20 * 65536, 1 << 20, 1 << 16 },
    {  75 * 65536,  20 * 65536, 1 << 20, 1 << 16 },
    { 140 * 65536,  20 * 65536, 1 << 20, 1 << 16 },
    { 205 * 65536,  20 * 65536, 1 << 20, 1 << 16 },
    {  10 * 65536, 100 * 65536, 1 << 20, 1 << 16 },
    {  75 * 65536, 100 * 65536, 1 << 20, 1 << 16 },
    { 140 * 65536, 100 * 65536, 1 << 20, 1 << 16 }
  };

/* the first two come before the wizard in list B */
static const s32 med_b[3][4] =
  {
    { 0x00378000, 0x00428000, 0x0009EB85, 0x0000A492 },
    { 0x00E48000, 0x003F8000, 0x000947AE, 0x00009999 },
    { 0x008A8000, 0x00728000, 0x0010A3D7, 0x000103A8 }
  };

static CCB *cc;
static u32 *bg_d;
static u32 *bg_v;
static u16 *bg_u;
static u16 *add_src;
static u16 *add_full;
static u8  *med_src;
static u32 *wiz_src;
static u16 *wiz_plut;
static Item target;
static Stat st;

static void sc_free(void)
{
  if(cc != NULL)       FreeMem(cc, CC_BYTES);
  if(bg_d != NULL)     FreeMem(bg_d, BG_BYTES);
  if(bg_v != NULL)     FreeMem(bg_v, BG_BYTES);
  if(bg_u != NULL)     FreeMem(bg_u, BGU_BYTES);
  if(add_src != NULL)  FreeMem(add_src, ADD_BYTES);
  if(add_full != NULL) FreeMem(add_full, ADD_BYTES);
  if(med_src != NULL)  FreeMem(med_src, MED_BYTES);
  if(wiz_src != NULL)  FreeMem(wiz_src, WIZ_BYTES);
  if(wiz_plut != NULL) FreeMem(wiz_plut, 64);
  cc = NULL;
  bg_d = NULL;
  bg_v = NULL;
  bg_u = NULL;
  add_src = NULL;
  add_full = NULL;
  med_src = NULL;
  wiz_src = NULL;
  wiz_plut = NULL;
}

static int sc_alloc(void)
{
  cc       = (CCB *)AllocMem(CC_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  bg_d     = (u32 *)AllocMem(BG_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  bg_v     = (u32 *)AllocMem(BG_BYTES, MEMTYPE_VRAM | MEMTYPE_CEL);
  bg_u     = (u16 *)AllocMem(BGU_BYTES, MEMTYPE_VRAM | MEMTYPE_CEL);
  add_src  = (u16 *)AllocMem(ADD_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  add_full = (u16 *)AllocMem(ADD_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  med_src  = (u8 *)AllocMem(MED_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  wiz_src  = (u32 *)AllocMem(WIZ_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  wiz_plut = (u16 *)AllocMem(64, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  if(cc == NULL || bg_d == NULL || bg_v == NULL || bg_u == NULL || add_src == NULL ||
     add_full == NULL || med_src == NULL || wiz_src == NULL || wiz_plut == NULL)
    {
      sc_free();
      return -1;
    }
  return 0;
}

static u32 bg_pixel(u32 x, u32 y)
{
  return MakeRGB15((x >> 5) & 31, (y * 4) & 31, 8 + (x & 15));
}

static void build_bg(void)
{
  u32 x;
  u32 y;
  u32 k;
  u32 n;
  u32 pos;
  u32 *row;

  for(y = 0; y < 8; y++)
    {
      row = bg_d + y * BG_WORDS;
      pos = 16;
      for(x = 0; x < BG_SIZE; x += n)
        {
          n = BG_SIZE - x;
          if(n > 64)
            n = 64;
          put_bits(row, &pos, PACK_LITERAL, 2);
          put_bits(row, &pos, n - 1, 6);
          for(k = 0; k < n; k++)
            put_bits(row, &pos, bg_pixel(x + k, y), 16);
        }
      put_bits(row, &pos, PACK_EOL, 2);
      row[0] |= (u32)(BG_WORDS - 2) << 16;
    }
  for(y = 8; y < BG_SIZE; y++)
    memcpy(bg_d + y * BG_WORDS, bg_d + (y & 7) * BG_WORDS, BG_WORDS * 4);
  memcpy(bg_v, bg_d, BG_BYTES);
  for(y = 0; y < BG_SIZE; y++)
    for(x = 0; x < BG_SIZE; x++)
      bg_u[y * BG_SIZE + x] = (u16)bg_pixel(x, y & 7);
}

static void build_add(void)
{
  s32 x;
  s32 y;
  s32 dx;
  s32 dy;
  u16 v;

  for(y = 0; y < ADD_SIZE; y++)
    for(x = 0; x < ADD_SIZE; x++)
      {
        dx = x - ADD_SIZE / 2;
        dy = y - ADD_SIZE / 2;
        v  = (u16)MakeRGB15(3 + (x & 3), 2 + (y & 3), 6);
        add_full[y * ADD_SIZE + x] = v;
        add_src[y * ADD_SIZE + x]  = (dx * dx + dy * dy < ADD_R2) ? v : 0;
      }
}

/* an ellipse in the middle of the 50 x 70 rows of 52 bytes */
static void build_med(void)
{
  s32 x;
  s32 y;
  s32 dx;
  s32 dy;

  for(y = 0; y < MED_H; y++)
    for(x = 0; x < MED_W; x++)
      {
        dx = 2 * x + 1 - MED_W;
        dy = 2 * y + 1 - MED_H;
        if(dx * dx * 4096 + dy * dy * 2209 < 9048064)
          med_src[y * MED_WORDS * 4 + x] = (u8)(0x49 + (x & 7) * 2);
      }
}

/* A figure 12 pixels narrower in the middle: a transparent margin, then 9 literal and
   4 repeated pixels in turn. About 14 packets and 16 words per row, as in the game. */
static void build_wiz(void)
{
  u32 y;
  u32 x;
  u32 k;
  u32 m;
  u32 pos;
  u32 words;
  u32 left;
  u32 right;
  u32 gap_at;
  u32 gap_done;
  u32 phase;
  u32 *row;

  for(k = 1; k < 32; k++)
    wiz_plut[k] = (u16)MakeRGB15(k, 31 - k, 16);
  wiz_plut[0] = 0;
  row = wiz_src;
  for(y = 0; y < WIZ_SIZE; y++)
    {
      pos = 8;
      if(y >= 8 && y <= 150)
        {
          left     = 54 - (y * 35) / 100;
          right    = 100 + (y * 38) / 100;
          gap_at   = (left + right) / 2 - 6;
          gap_done = 0;
          phase    = 0;
          put_bits(row, &pos, PACK_TRANSPARENT, 2);
          put_bits(row, &pos, left - 1, 6);
          x = left;
          while(x < right)
            {
              if(!gap_done && x >= gap_at)
                {
                  put_bits(row, &pos, PACK_TRANSPARENT, 2);
                  put_bits(row, &pos, 12 - 1, 6);
                  x += 12;
                  gap_done = 1;
                  continue;
                }
              m = (phase == 0) ? 9 : 4;
              if(m > right - x)
                m = right - x;
              if(phase == 0)
                {
                  put_bits(row, &pos, PACK_LITERAL, 2);
                  put_bits(row, &pos, m - 1, 6);
                  for(k = 0; k < m; k++)
                    put_bits(row, &pos, ((x + k) % 7 == 0) ? 0 : 1 + (x + k) % 31, 6);
                }
              else
                {
                  put_bits(row, &pos, PACK_PACKED, 2);
                  put_bits(row, &pos, m - 1, 6);
                  put_bits(row, &pos, 1 + (x >> 2) % 31, 6);
                }
              x += m;
              phase = 1 - phase;
            }
        }
      put_bits(row, &pos, PACK_EOL, 2);
      words = (pos + 31) >> 5;
      if(words < 2)
        words = 2;
      row[0] |= (words - 2) << 24;
      row += words;
    }
}

static void cel_bg(CCB *c, const s32 *g)
{
  init_ccb(c, BG_FLAGS, bg_v, NULL, BG_PRE0, BG_PRE1, PIXC_SOLID);
  c->ccb_XPos   = g[0];
  c->ccb_YPos   = g[1];
  c->ccb_HDX    = g[2];
  c->ccb_HDY    = g[3];
  c->ccb_VDX    = g[4];
  c->ccb_VDY    = g[5];
  c->ccb_Width  = BG_SIZE;
  c->ccb_Height = BG_SIZE;
}

static void cel_add(CCB *c, const s32 *g, void *src)
{
  init_ccb(c, ADD_FLAGS, src, NULL, ADD_PRE0, ADD_PRE1, ADD_PIXC);
  c->ccb_XPos   = g[0];
  c->ccb_YPos   = g[1];
  c->ccb_HDX    = g[2];
  c->ccb_VDY    = g[3];
  c->ccb_Width  = ADD_SIZE;
  c->ccb_Height = ADD_SIZE;
}

static void cel_med(CCB *c, const s32 *g)
{
  init_ccb(c, ADD_FLAGS, med_src, NULL, MED_PRE0, MED_PRE1, ADD_PIXC);
  c->ccb_XPos   = g[0];
  c->ccb_YPos   = g[1];
  c->ccb_HDX    = g[2];
  c->ccb_VDY    = g[3];
  c->ccb_Width  = MED_W;
  c->ccb_Height = MED_H;
}

static void cel_wiz(CCB *c)
{
  init_ccb(c, WIZ_FLAGS, wiz_src, wiz_plut, WIZ_PRE0, WIZ_PRE1, PIXC_SOLID);
  c->ccb_XPos   = WIZ_X;
  c->ccb_YPos   = WIZ_Y;
  c->ccb_Width  = WIZ_SIZE;
  c->ccb_Height = WIZ_SIZE;
}

/* links the first n cels and sets, on every one, the flags in or_flags */
static void close_list(u32 n, u32 or_flags)
{
  u32 i;

  for(i = 0; i < n; i++)
    {
      cc[i].ccb_Flags  |= or_flags;
      cc[i].ccb_Flags  &= ~CCB_LAST;
      cc[i].ccb_NextPtr = &cc[i + 1];
    }
  cc[n - 1].ccb_Flags  |= CCB_LAST;
  cc[n - 1].ccb_NextPtr = NULL;
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

static void list_a(u32 or_flags)
{
  u32 i;

  cel_bg(&cc[0], bg_a);
  for(i = 0; i < 4; i++)
    cel_add(&cc[1 + i], add_a[i], add_src);
  cel_wiz(&cc[5]);
  for(i = 4; i < 7; i++)
    cel_add(&cc[2 + i], add_a[i], add_src);
  close_list(9, or_flags);
}

static void list_b(u32 or_flags)
{
  cel_bg(&cc[0], bg_b);
  cel_med(&cc[1], med_b[0]);
  cel_med(&cc[2], med_b[1]);
  cel_wiz(&cc[3]);
  cel_med(&cc[4], med_b[2]);
  close_list(5, or_flags);
}

static void only_bg(const s32 *g, u32 or_flags)
{
  cel_bg(&cc[0], g);
  close_list(1, or_flags);
}

static void only_add(const s32 g[7][4], void *src, u32 or_flags)
{
  u32 i;

  for(i = 0; i < 7; i++)
    cel_add(&cc[i], g[i], src);
  close_list(7, or_flags);
}

static void only_wiz(u32 or_flags)
{
  cel_wiz(&cc[0]);
  close_list(1, or_flags);
}

static void only_med(void)
{
  u32 i;

  for(i = 0; i < 3; i++)
    cel_med(&cc[i], med_b[i]);
  close_list(3, 0);
}

/* the fields and the microseconds taken by LOOPS lists, each followed by one WaitVBL */
static void loop_test(void)
{
  u32 i;
  u32 t0;
  u32 v0;
  u32 fields;
  u32 us;

  WaitVBL(b_vbl_ioreq, 1);
  v0 = vbl_now();
  t0 = usec_now();
  for(i = 0; i < LOOPS; i++)
    {
      DrawCels(target, cc);
      WaitVBL(b_vbl_ioreq, 1);
    }
  us     = usec_now() - t0;
  fields = vbl_now() - v0;
  grid_u(fields);
  grid_u(us);
}

static u32 list_crc(void)
{
  SetVRAMPages(b_vram_ioreq, b_sc.sc_Bitmaps[1]->bm_Buffer, 0, (s32)b_sc.sc_NumBitmapPages, 0xFFFFFFFF);
  DrawCels(target, cc);
  return crc32_update(0, (const u8 *)b_sc.sc_Bitmaps[1]->bm_Buffer, b_sc.sc_NumBitmapBytes);
}

void group_scene(void)
{
  char *p;
  u32 crc_a;
  u32 crc_b;
  u32 i;

  if(sc_alloc() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress(L("PREPARATION", "PREPARING  "));
  build_bg();
  build_add();
  build_med();
  build_wiz();
  target = b_sc.sc_BitmapItems[1];

  grid_begin('W', 4);

  progress(L("LISTE A", "LIST A "));
  list_a(0);                      measure();
  only_bg(bg_a, 0);               measure();
  only_add(add_a, add_src, 0);    measure();
  only_wiz(0);                    measure();

  progress(L("LISTE A AVEC ACE", "LIST A WITH ACE "));
  list_a(CCB_ACE);                measure();
  only_bg(bg_a, CCB_ACE);         measure();
  only_add(add_a, add_src, CCB_ACE); measure();
  only_wiz(CCB_ACE);              measure();

  progress(L("FOND", "BKGD"));
  only_bg(bg_a, 0);
  cc[0].ccb_SourcePtr = (CelData *)bg_d;
  measure();
  only_bg(bg_flat, 0);            measure();
  only_bg(bg_one, 0);             measure();
  only_bg(bg_a, 0);
  cc[0].ccb_SourcePtr = (CelData *)bg_u;
  cc[0].ccb_Flags &= ~CCB_PACKED;
  measure();

  progress(L("AJOUTS", "EXTRAS"));
  only_bg(bg_a, 0);
  cc[0].ccb_Flags &= ~CCB_USEAV;
  measure();
  only_add(add_a, add_src, 0);
  for(i = 0; i < 7; i++)
    cc[i].ccb_PIXC = PIXC_SOLID;
  measure();
  only_add(add_a, add_full, 0);   measure();
  only_add(add_one, add_src, 0);  measure();

  progress(L("LISTE B", " LIST B"));
  list_b(0);                      measure();
  only_bg(bg_b, 0);               measure();
  only_med();                     measure();
  list_b(CCB_ACE);                measure();

  progress(L("BOUCLES", "LOOPS  "));
  list_a(0);
  loop_test();
  list_b(0);
  loop_test();
  grid_end();

  progress("CRC");
  list_a(0);
  crc_a = list_crc();
  list_b(0);
  crc_b = list_crc();
  p = new_line("CRC A ");
  p = put_x(p, crc_a, 8);
  p = put_s(p, " B ");
  p = put_x(p, crc_b, 8);
  (void)p;
  log_line();

  sc_free();
}

/* ---------------------------------------------------------------- version 3.5 */

/* RENDU DETAIL 2/2, grid K: the wizard scene cel by cel, the hash of the screen after each of the first k cels of list A (9 cels),
   then of list B (5 cels); then the CRC of both whole lists (SCENE MAGICIEN on the FZ-10: A 90B45BE9, B 0E3CEBCB) */
static void prefix_hashes(u32 n)
{
  u32 k;
  u32 fl;
  u32 crc;
  CCB *nx;

  for(k = 1; k <= n; k++)
    {
      fl = cc[k - 1].ccb_Flags;
      nx = cc[k - 1].ccb_NextPtr;
      cc[k - 1].ccb_Flags |= CCB_LAST;
      cc[k - 1].ccb_NextPtr = NULL;
      crc = list_crc();
      cc[k - 1].ccb_Flags = fl;
      cc[k - 1].ccb_NextPtr = nx;
      grid_x((crc ^ (crc >> 16)) & 0xFFFF, 4);
    }
}

void scene_detail(void)
{
  char *p;
  u32 crc_a;
  u32 crc_b;

  if(sc_alloc() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress("SCENE DETAIL");
  build_bg();
  build_add();
  build_med();
  build_wiz();
  target = b_sc.sc_BitmapItems[1];
  grid_begin('K', 7);
  list_a(0);
  prefix_hashes(9);
  list_b(0);
  prefix_hashes(5);
  grid_end();
  list_a(0);
  crc_a = list_crc();
  list_b(0);
  crc_b = list_crc();
  p = new_line("CRC A ");
  p = put_x(p, crc_a, 8);
  p = put_s(p, " B ");
  p = put_x(p, crc_b, 8);
  (void)p;
  log_line();
  sc_free();
}

/* ---------------------------------------------------------------- version 3.6 */

/* SCENE BANDES 1/2, grid I, six hashes per row: the screen after the first 7 cels of list A (the first prefix whose
   hash differs between the FZ-10 and the core on RENDU DETAIL 2/2: an addition drawn over the wizard), then after the
   5 cels of list B (the last one, a medallion over the wizard, differs too). For each, 12 bands of 20 lines from the
   top, and in each band the thirds x 0-106, 107-213, 214-319: 36 hashes, two bands per row (I0-I5 list A, I6-IB
   list B). The frame buffer holds two lines in each 32-bit word (line pairs). */
static void band_hashes(u32 n)
{
  static const u16 x0[4] = { 0, 107, 214, 320 };
  const u8 *fb;
  CCB *nx;
  u32 fl;
  u32 crc;
  u32 b;
  u32 t;
  u32 pr;

  fl = cc[n - 1].ccb_Flags;
  nx = cc[n - 1].ccb_NextPtr;
  cc[n - 1].ccb_Flags |= CCB_LAST;
  cc[n - 1].ccb_NextPtr = NULL;
  (void)list_crc();
  cc[n - 1].ccb_Flags = fl;
  cc[n - 1].ccb_NextPtr = nx;
  fb = (const u8 *)b_sc.sc_Bitmaps[1]->bm_Buffer;
  for(b = 0; b < 12; b++)
    for(t = 0; t < 3; t++)
      {
        crc = 0;
        for(pr = b * 10; pr < b * 10 + 10; pr++)
          crc = crc32_update(crc, fb + (pr * SCREEN_W + x0[t]) * 4, (u32)(x0[t + 1] - x0[t]) * 4);
        grid_x((crc ^ (crc >> 16)) & 0xFFFF, 4);
      }
}

void scene_bands(void)
{
  if(sc_alloc() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress(L("SCENE BANDES", "SCENE BANDS "));
  build_bg();
  build_add();
  build_med();
  build_wiz();
  target = b_sc.sc_BitmapItems[1];
  grid_begin('I', 6);
  list_a(0);
  band_hashes(7);
  list_b(0);
  band_hashes(5);
  grid_end();
  sc_free();
}

/* SCENE BANDES 2/2, grid O (the letter), seven hashes per row: CRC of the screen after an addition over known pixels.
    0- 7  a 16-bit square of 64 x 64 at (128, 88) holding one value, 0000 0001 8000 8001 4210 4211 7FFF FFFF, then the
          addition cel of the scene (60 x 60, 16 bits, PIXC 1F001F80) over it at scale 1
    8-15  the same, the addition at its scale in list A (0.5 x 0.53)
   16-23  the same, the medallion (50 x 70, 8 bits, same PIXC) at scale 1
   24-26  the wizard alone; the wizard then the 5th addition of list A at its place; the wizard then the 3rd medallion
          of list B at its place */
#define SQ_W 64

static u16 *sq_src;

static void square(CCB *c, u16 v)
{
  u32 i;

  for(i = 0; i < SQ_W * SQ_W; i++)
    sq_src[i] = v;
  init_ccb(c, CCB_NPABS | CCB_SPABS | CCB_PPABS | CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_CCBPRE | CCB_YOXY |
           CCB_ACW | CCB_ACCW | CCB_BGND | CCB_NOBLK, sq_src, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((SQ_W - 1) << PRE0_VCNT_SHIFT),
           ((SQ_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (SQ_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
  c->ccb_XPos   = (Coord)(128 << 16);
  c->ccb_YPos   = (Coord)(88 << 16);
  c->ccb_Width  = SQ_W;
  c->ccb_Height = SQ_W;
}

static void two_crc(void)
{
  u32 crc;

  close_list(2, 0);
  crc = list_crc();
  grid_x((crc ^ (crc >> 16)) & 0xFFFF, 4);
}

void scene_adds(void)
{
  static const u16 dv[8] = { 0x0000, 0x0001, 0x8000, 0x8001, 0x4210, 0x4211, 0x7FFF, 0xFFFF };
  static const s32 g1[4] = { 130 << 16, 90 << 16, 1 << 20, 1 << 16 };
  static const s32 gs[4] = { 144 << 16, 104 << 16, 0x00080000, 0x00008888 };
  static const s32 gm[4] = { 135 << 16, 85 << 16, 1 << 20, 1 << 16 };
  u32 i;
  u32 crc;

  if(sc_alloc() < 0)
    {
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  sq_src = (u16 *)AllocMem(SQ_W * SQ_W * 2, MEMTYPE_DRAM | MEMTYPE_CEL);
  if(sq_src == NULL)
    {
      sc_free();
      text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
      return;
    }
  progress(L("ADDITIONS", "ADDITIONS"));
  build_add();
  build_med();
  build_wiz();
  target = b_sc.sc_BitmapItems[1];
  grid_begin('O', 7);
  for(i = 0; i < 8; i++)
    {
      square(&cc[0], dv[i]);
      cel_add(&cc[1], g1, add_src);
      two_crc();
    }
  for(i = 0; i < 8; i++)
    {
      square(&cc[0], dv[i]);
      cel_add(&cc[1], gs, add_src);
      two_crc();
    }
  for(i = 0; i < 8; i++)
    {
      square(&cc[0], dv[i]);
      cel_med(&cc[1], gm);
      two_crc();
    }
  only_wiz(0);
  crc = list_crc();
  grid_x((crc ^ (crc >> 16)) & 0xFFFF, 4);
  cel_wiz(&cc[0]);
  cel_add(&cc[1], add_a[4], add_src);
  two_crc();
  cel_wiz(&cc[0]);
  cel_med(&cc[1], med_b[2]);
  two_crc();
  grid_end();
  FreeMem(sq_src, SQ_W * SQ_W * 2);
  sq_src = NULL;
  sc_free();
}
