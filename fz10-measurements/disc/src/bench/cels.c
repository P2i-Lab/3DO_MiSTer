/*
  P2i Lab 3DO bench: cel data shared by the cel groups.

  C_FILL and C_IMAGE copy the formats of the two cels of the intro slide show of
  Strahl (USA): a 1-pixel 16-bit cel stretched over the whole screen (CCB flags
  0x57260030, PRE0 0x16), then a 320x240 packed 6-bit coded cel (flags 0x7FE60600,
  PRE0 0x3BC4). Their pixels are generated here.
*/

#include "bench.h"

#include "mem.h"
#include "string.h"

#define CCB_COUNT    (C_SMALL + SMALL_CELS)
#define ROW_WORDS    64           /* room per packed row; 320 literal 6-bit pixels take 62 words */

#define CCB_BYTES    (CCB_COUNT * sizeof(CCB))
#define FILL_BYTES   16
#define IMAGE_BYTES  ((SCREEN_H * ROW_WORDS + 4) * 4)
#define PLUT_BYTES   64
#define SRC16_BYTES  (SCREEN_W * SCREEN_H * 2)
#define SRC8_BYTES   (SCREEN_W * SCREEN_H)
#define SMALL_BYTES  (8 * 8 * 2)

CCB *b_ccbs;
u16 *b_src16;
u16 *b_plut;

static u32 *fill_src;
static u32 *image_src;
static u8  *src8;
static u16 *src_small;

/* writes the nbits low bits of value, most significant first, in big-endian words */
void put_bits(u32 *buf, u32 *pos, u32 value, u32 nbits)
{
  u32 word;
  u32 used;
  u32 room;
  u32 take;
  u32 chunk;

  while(nbits != 0)
    {
      word  = *pos >> 5;
      used  = *pos & 31;
      room  = 32 - used;
      take  = (nbits < room) ? nbits : room;
      chunk = (value >> (nbits - take)) & ((take == 32) ? 0xFFFFFFFF : ((1u << take) - 1));
      buf[word] |= chunk << (room - take);
      *pos  += take;
      nbits -= take;
    }
}

/* rings around the centre, values 1 to 31 */
static u32 ring_pixel(u32 x, u32 y)
{
  s32 dx;
  s32 dy;

  dx = (s32)x - SCREEN_W / 2;
  dy = (s32)y - SCREEN_H / 2;
  return 1 + ((u32)((dx * dx + dy * dy) / 128) % 31);
}

/* one packed 6-bit row made of literal packets; returns its length in words */
static u32 pack_row(u32 *buf, u32 row_word, u32 y)
{
  u32 pos;
  u32 x;
  u32 n;
  u32 k;
  u32 words;

  pos = row_word * 32;
  put_bits(buf, &pos, 0, 8);           /* offset to the next row, filled in below */
  for(x = 0; x < SCREEN_W; x += n)
    {
      n = SCREEN_W - x;
      if(n > 64)
        n = 64;
      put_bits(buf, &pos, PACK_LITERAL, 2);
      put_bits(buf, &pos, n - 1, 6);
      for(k = 0; k < n; k++)
        put_bits(buf, &pos, ring_pixel(x + k, y), 6);
    }
  put_bits(buf, &pos, PACK_EOL, 2);
  words = ((pos + 31) >> 5) - row_word;
  if(words < 2)
    words = 2;
  buf[row_word] |= (words - 2) << 24;
  return words;
}

void init_ccb(CCB *c, u32 flags, void *src, void *lut, u32 pre0, u32 pre1, u32 pixc)
{
  memset(c, 0, sizeof(CCB));
  c->ccb_Flags     = flags;
  c->ccb_NextPtr   = NULL;
  c->ccb_SourcePtr = (CelData *)src;
  c->ccb_PLUTPtr   = lut;
  c->ccb_XPos      = 0;
  c->ccb_YPos      = 0;
  c->ccb_HDX       = 1 << 20;
  c->ccb_HDY       = 0;
  c->ccb_VDX       = 0;
  c->ccb_VDY       = 1 << 16;
  c->ccb_HDDX      = 0;
  c->ccb_HDDY      = 0;
  c->ccb_PIXC      = pixc;
  c->ccb_PRE0      = pre0;
  c->ccb_PRE1      = pre1;
  c->ccb_Width     = SCREEN_W;
  c->ccb_Height    = SCREEN_H;
}

void cels_free(void)
{
  if(b_ccbs != NULL)    FreeMem(b_ccbs, CCB_BYTES);
  if(fill_src != NULL)  FreeMem(fill_src, FILL_BYTES);
  if(image_src != NULL) FreeMem(image_src, IMAGE_BYTES);
  if(b_plut != NULL)    FreeMem(b_plut, PLUT_BYTES);
  if(b_src16 != NULL)   FreeMem(b_src16, SRC16_BYTES);
  if(src8 != NULL)      FreeMem(src8, SRC8_BYTES);
  if(src_small != NULL) FreeMem(src_small, SMALL_BYTES);
  b_ccbs = NULL;
  fill_src = NULL;
  image_src = NULL;
  b_plut = NULL;
  b_src16 = NULL;
  src8 = NULL;
  src_small = NULL;
}

int cels_setup(void)
{
  u32 i;
  u32 x;
  u32 y;
  u32 row;
  u32 flags16;
  CCB *c;

  if(b_ccbs != NULL)
    return 0;

  b_ccbs    = (CCB *)AllocMem(CCB_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  fill_src  = (u32 *)AllocMem(FILL_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  image_src = (u32 *)AllocMem(IMAGE_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  b_plut    = (u16 *)AllocMem(PLUT_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL | MEMTYPE_FILL);
  b_src16   = (u16 *)AllocMem(SRC16_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL);
  src8      = (u8 *)AllocMem(SRC8_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL);
  src_small = (u16 *)AllocMem(SMALL_BYTES, MEMTYPE_DRAM | MEMTYPE_CEL);
  if(b_ccbs == NULL || fill_src == NULL || image_src == NULL || b_plut == NULL ||
     b_src16 == NULL || src8 == NULL || src_small == NULL)
    {
      cels_free();
      return -1;
    }

  for(i = 0; i < 32; i++)
    b_plut[i] = (u16)MakeRGB15(i, (i * 2) & 31, 31 - i);
  for(y = 0; y < SCREEN_H; y++)
    for(x = 0; x < SCREEN_W; x++)
      {
        b_src16[y * SCREEN_W + x] = (u16)MakeRGB15((x >> 3) & 31, (y >> 3) & 31, 12);
        src8[y * SCREEN_W + x]    = (u8)(1 + ((x >> 4) + (y >> 4)) % 31);
      }
  for(i = 0; i < 64; i++)
    src_small[i] = (u16)MakeRGB15(31, 16, 4);

  /* Strahl's fill: preamble in the source, 16 bits uncoded, 1 line of 1 pixel */
  fill_src[0] = PRE0_LINEAR | PRE0_BPP_16;
  fill_src[1] = 0;
  fill_src[2] = (u32)MakeRGB15(6, 5, 3) << 16;
  fill_src[3] = 0;
  c = &b_ccbs[C_FILL];
  init_ccb(c, CCB_LAST | CCB_SPABS | CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_YOXY |
           CCB_ACW | CCB_ACCW | CCB_BGND | CCB_NOBLK, fill_src, NULL, 0, 0, PIXC_SOLID);
  c->ccb_HDX = SCREEN_W << 20;
  c->ccb_VDY = SCREEN_H << 16;

  /* the same 1-pixel cel, not stretched: cost of one DrawCels call */
  c = &b_ccbs[C_DOT];
  *c = b_ccbs[C_FILL];
  c->ccb_HDX = 1 << 20;
  c->ccb_VDY = 1 << 16;

  /* Strahl's slide: 320x240 packed 6 bits coded, preamble in the CCB */
  row = 0;
  for(y = 0; y < SCREEN_H; y++)
    row += pack_row(image_src, row, y);
  init_ccb(&b_ccbs[C_IMAGE], CCB_LAST | CCB_NPABS | CCB_SPABS | CCB_PPABS | CCB_LDSIZE |
           CCB_LDPRS | CCB_LDPPMP | CCB_LDPLUT | CCB_CCBPRE | CCB_YOXY | CCB_ACW |
           CCB_ACCW | CCB_USEAV | CCB_PACKED, image_src, b_plut,
           PRE0_BPP_6 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT), 0, PIXC_SOLID);

  /* 320x240 16 bits uncoded unpacked */
  flags16 = CCB_LAST | CCB_NPABS | CCB_SPABS | CCB_PPABS | CCB_LDSIZE | CCB_LDPRS |
            CCB_LDPPMP | CCB_CCBPRE | CCB_YOXY | CCB_ACW | CCB_ACCW | CCB_BGND | CCB_NOBLK;
  init_ccb(&b_ccbs[C_16BPP], flags16, b_src16, NULL,
           PRE0_LINEAR | PRE0_BPP_16 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SCREEN_W * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);

  /* the same, averaged with the frame buffer (reads the frame buffer) */
  b_ccbs[C_HALF] = b_ccbs[C_16BPP];
  b_ccbs[C_HALF].ccb_PIXC = PIXC_HALF;

  /* 320x240 8 bits coded unpacked */
  init_ccb(&b_ccbs[C_8BPP], flags16 | CCB_LDPLUT, src8, b_plut,
           PRE0_BPP_8 | ((SCREEN_H - 1) << PRE0_VCNT_SHIFT),
           ((SCREEN_W / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
           PRE1_TLLSB_PDC0 | (SCREEN_W - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);

  /* a list of 8x8 16-bit cels */
  for(i = 0; i < SMALL_CELS; i++)
    {
      c = &b_ccbs[C_SMALL + i];
      init_ccb(c, flags16 & ~CCB_LAST, src_small, NULL,
               PRE0_LINEAR | PRE0_BPP_16 | (7 << PRE0_VCNT_SHIFT),
               ((8 * 2 / 4 - PRE1_WOFFSET_PREFETCH) << PRE1_WOFFSET10_SHIFT) |
               PRE1_TLLSB_PDC0 | (8 - PRE1_TLHPCNT_PREFETCH), PIXC_SOLID);
      c->ccb_XPos = (Coord)(((i * 37) % (SCREEN_W - 8)) << 16);
      c->ccb_YPos = (Coord)(((i * 23) % (SCREEN_H - 8)) << 16);
      if(i + 1 < SMALL_CELS)
        c->ccb_NextPtr = &b_ccbs[C_SMALL + i + 1];
      else
        c->ccb_Flags |= CCB_LAST;
    }

  return 0;
}

/* draws a (then b) into the hidden screen reps times, each time timed */
void time_cels(const char *name, CCB *a, CCB *b, u32 reps)
{
  static Stat s;
  u32 i;
  u32 t0;

  progress(name);
  stat_reset(&s);
  for(i = 0; i < reps; i++)
    {
      t0 = usec_now();
      DrawCels(b_sc.sc_BitmapItems[1], a);
      if(b != NULL)
        DrawCels(b_sc.sc_BitmapItems[1], b);
      stat_add(&s, usec_now() - t0);
    }
  stat_line(name, &s);
}
