/*
  P2i Lab 3DO bench, group MATH MADAM: the matrix functions of the Operamath folio
  (done by MADAM's matrix engine, including the divide by Z), fed with the same
  pseudo-random inputs everywhere. Each line gives the CRC32 of all the results and
  the time of the whole run. A different CRC means a different result somewhere.
*/

#include "bench.h"

#include "operamath.h"
#include "string.h"

#define N1   256
#define N2   128
#define NMAT 16

static u32 seed;

static vec3f16 vin[N1];
static vec3f16 vz[N1];
static vec3f16 va[N1];
static vec3f16 vb[N1];
static vec3f16 vout[N1];
static vec4f16 v4in[N1];
static vec4f16 v4out[N1];
static mat33f16 ma[NMAT];
static mat33f16 mb[NMAT];
static mat33f16 mz;
static mat33f16 m3out[N2];
static mat44f16 m4a[NMAT];
static mat44f16 m4b[NMAT];
static mat44f16 m4out[N2];
static frac16 fa[N1];
static frac16 fb[N1];
static frac16 fout[N1];
static frac16 sw[N1][4];

/* uniform in [-range, range) as 16.16, range in whole units */
static frac16 rnd(u32 range)
{
  return (frac16)(rand_next(&seed) % (range << 17)) - (frac16)(range << 16);
}

static void math_line(const char *name, u32 crc, u32 us)
{
  char *p;

  p = new_line(name);
  p = put_x(p, crc, 8);
  p = put_u(p, us, 8);
  p = put_s(p, " US");
  (void)p;
  log_line();
}

static void make_inputs(void)
{
  u32 i;
  u32 j;
  u32 k;
  frac16 z;

  seed = 0x3D0F2010;
  for(i = 0; i < N1; i++)
    {
      for(j = 0; j < 3; j++)
        {
          vin[i][j] = rnd(256);
          va[i][j]  = rnd(16);
          vb[i][j]  = rnd(16);
        }
      for(j = 0; j < 4; j++)
        v4in[i][j] = rnd(256);
      /* z kept away from 0: 16 to 255, either sign */
      z = (frac16)((16 + rand_next(&seed) % 240) << 16) + (frac16)(rand_next(&seed) & 0xFFFF);
      if(rand_next(&seed) & 0x100)
        z = -z;
      vz[i][0] = rnd(256);
      vz[i][1] = rnd(256);
      vz[i][2] = z;
      fa[i] = rnd(128);
      fb[i] = rnd(128);
    }
  for(k = 0; k < NMAT; k++)
    for(i = 0; i < 3; i++)
      for(j = 0; j < 3; j++)
        {
          ma[k][i][j] = rnd(2);
          mb[k][i][j] = rnd(2);
        }
  for(k = 0; k < NMAT; k++)
    for(i = 0; i < 4; i++)
      for(j = 0; j < 4; j++)
        {
          m4a[k][i][j] = rnd(2);
          m4b[k][i][j] = rnd(2);
        }
  /* x and y mixed, z left alone whichever way the product is taken */
  for(i = 0; i < 3; i++)
    for(j = 0; j < 3; j++)
      mz[i][j] = (i < 2 && j < 2) ? rnd(2) : 0;
  mz[2][2] = 1 << 16;
}

void group_math(void)
{
  mmv3m33d many;
  u32 i;
  Clock ck;
  u32 t;
  frac16 n;

  make_inputs();
  n = 256 << 16;

  clock_start(&ck);
  for(i = 0; i < N1; i++)
    MulVec3Mat33_F16(vout[i], vin[i], ma[i % NMAT]);
  t = clock_us(&ck);
  math_line("V3XM33    ", crc32_update(0, (const u8 *)vout, sizeof(vout)), t);

  clock_start(&ck);
  for(i = 0; i < N2; i++)
    MulMat33Mat33_F16(m3out[i], ma[i % NMAT], mb[(i * 7) % NMAT]);
  t = clock_us(&ck);
  math_line("M33XM33   ", crc32_update(0, (const u8 *)m3out, sizeof(m3out)), t);

  clock_start(&ck);
  for(i = 0; i < N1; i++)
    MulVec4Mat44_F16(v4out[i], v4in[i], m4a[i % NMAT]);
  t = clock_us(&ck);
  math_line("V4XM44    ", crc32_update(0, (const u8 *)v4out, sizeof(v4out)), t);

  clock_start(&ck);
  for(i = 0; i < N2; i++)
    MulMat44Mat44_F16(m4out[i], m4a[i % NMAT], m4b[(i * 5) % NMAT]);
  t = clock_us(&ck);
  math_line("M44XM44   ", crc32_update(0, (const u8 *)m4out, sizeof(m4out)), t);

  memset(vout, 0, sizeof(vout));
  clock_start(&ck);
  MulManyVec3Mat33_F16(vout, vin, ma[3], N1);
  t = clock_us(&ck);
  math_line("MANY V3   ", crc32_update(0, (const u8 *)vout, sizeof(vout)), t);

  memset(vout, 0, sizeof(vout));
  many.dest  = vout;
  many.src   = vz;
  many.mat   = &mz;
  many.n     = n;
  many.count = N1;
  clock_start(&ck);
  MulManyVec3Mat33DivZ_F16(&many);
  t = clock_us(&ck);
  math_line("DIVZ MANY ", crc32_update(0, (const u8 *)vout, sizeof(vout)), t);

  memset(vout, 0, sizeof(vout));
  clock_start(&ck);
  for(i = 0; i < N1; i++)
    MulVec3Mat33DivZ_F16(vout[i], vz[i], mz, n);
  t = clock_us(&ck);
  math_line("DIVZ 1    ", crc32_update(0, (const u8 *)vout, sizeof(vout)), t);

  clock_start(&ck);
  for(i = 0; i < N1; i++)
    fout[i] = Dot3_F16(va[i], vb[i]);
  t = clock_us(&ck);
  math_line("DOT3      ", crc32_update(0, (const u8 *)fout, sizeof(fout)), t);

  clock_start(&ck);
  for(i = 0; i < N1; i++)
    Cross3_F16(vout[i], va[i], vb[i]);
  t = clock_us(&ck);
  math_line("CROSS3    ", crc32_update(0, (const u8 *)vout, sizeof(vout)), t);

  clock_start(&ck);
  MulManyF16(fout, fa, fb, N1);
  t = clock_us(&ck);
  math_line("MANY F16  ", crc32_update(0, (const u8 *)fout, sizeof(fout)), t);

  clock_start(&ck);
  MulScalarF16(fout, fa, 0x00018000, N1);
  t = clock_us(&ck);
  math_line(L("SCALAIRE  ", "SCALAR    "), crc32_update(0, (const u8 *)fout, sizeof(fout)), t);

  clock_start(&ck);
  for(i = 0; i < N1; i++)
    fout[i] = AbsVec3_F16(vin[i]);
  t = clock_us(&ck);
  math_line("ABS V3    ", crc32_update(0, (const u8 *)fout, sizeof(fout)), t);

  /* done by the CPU: the same everywhere unless the CPU itself differs */
  clock_start(&ck);
  for(i = 0; i < N1; i++)
    {
      sw[i][0] = DivSF16(fa[i], fb[i] | 0x100);
      sw[i][1] = (frac16)SqrtF16((ufrac16)((fa[i] < 0) ? -fa[i] : fa[i]));
      sw[i][2] = SinF16(fa[i]);
      sw[i][3] = MulSF16(fa[i], fb[i]);
    }
  t = clock_us(&ck);
  math_line(L("LOGICIEL  ", "SOFTWARE  "), crc32_update(0, (const u8 *)sw, sizeof(sw)), t);
}

/* ---------------------------------------------------------------- version 3.0 */

static void raw_line(const char *name, const frac16 *v)
{
  char *p;

  p = new_line(name);
  p = put_x(p, (u32)v[0], 8);
  p = put_s(p, " ");
  p = put_x(p, (u32)v[1], 8);
  p = put_s(p, " ");
  p = put_x(p, (u32)v[2], 8);
  (void)p;
  log_line();
}

static u32 divz_crc(frac16 n)
{
  mmv3m33d many;

  memset(vout, 0, sizeof(vout));
  many.dest  = vout;
  many.src   = vz;
  many.mat   = &mz;
  many.n     = n;
  many.count = N1;
  MulManyVec3Mat33DivZ_F16(&many);
  return crc32_update(0, (const u8 *)vout, sizeof(vout));
}

/* extreme values: 16.16 limits and near limits */
static frac16 extreme(void)
{
  static const u32 pool[8] =
    {
      0x7FFFFFFF, 0x80000000, 0x00010000, 0xFFFF0000,
      0x00000001, 0xFFFFFFFF, 0x7FFF0000, 0x80010000
    };

  return (frac16)pool[(rand_next(&seed) >> 16) & 7];
}

void group_math2(void)
{
  mmv3m33d many;
  u32 i;
  u32 j;
  u32 k;

  make_inputs();

  /* DIVZ: numerators the first bench did not try */
  math_line("N NEG     ", divz_crc(-(256 << 16)), 0);
  raw_line("N NEG V0  ", vout[0]);
  math_line("N FRAC    ", divz_crc((256 << 16) + 0x1234), 0);
  math_line(L("N PETIT   ", "N SMALL   "), divz_crc(3), 0);
  math_line(L("N GRAND   ", "N LARGE   "), divz_crc(0x7FFF0000), 0);
  raw_line(L("N GRAND V0", "N LARGE V0"), vout[0]);
  math_line(L("N NEG FR  ", "N NEG FRAC"), divz_crc(-((256 << 16) + 0x1234)), 0);

  /* very small z, of either sign */
  for(i = 0; i < N1; i++)
    {
      va[i][0] = vz[i][0];
      va[i][1] = vz[i][1];
      va[i][2] = (i & 1) ? -(frac16)(1 + (i % 64)) : (frac16)(1 + (i % 64));
    }
  memset(vout, 0, sizeof(vout));
  many.dest  = vout;
  many.src   = va;
  many.mat   = &mz;
  many.n     = 256 << 16;
  many.count = N1;
  MulManyVec3Mat33DivZ_F16(&many);
  math_line(L("Z PETIT   ", "Z SMALL   "), crc32_update(0, (const u8 *)vout, sizeof(vout)), 0);
  raw_line(L("Z PETIT V0", "Z SMALL V0"), vout[0]);

  /* z = 0 */
  for(i = 0; i < 4; i++)
    {
      va[i][0] = (frac16)(((i & 1) ? -3 : 3) * 65536);
      va[i][1] = (frac16)(((i & 2) ? -5 : 5) * 65536);
      va[i][2] = 0;
    }
  memset(vout, 0, sizeof(vout));
  many.count = 4;
  MulManyVec3Mat33DivZ_F16(&many);
  raw_line(L("Z NUL V0  ", "Z ZERO V0 "), vout[0]);
  raw_line(L("Z NUL V1  ", "Z ZERO V1 "), vout[1]);
  raw_line(L("Z NUL V3  ", "Z ZERO V3 "), vout[3]);

  /* overflow of the accumulators */
  seed = 0xE7E70001;
  for(i = 0; i < N1; i++)
    for(j = 0; j < 3; j++)
      vin[i][j] = extreme();
  for(k = 0; k < NMAT; k++)
    for(i = 0; i < 3; i++)
      for(j = 0; j < 3; j++)
        ma[k][i][j] = extreme();
  for(i = 0; i < N1; i++)
    for(j = 0; j < 4; j++)
      v4in[i][j] = extreme();
  for(k = 0; k < NMAT; k++)
    for(i = 0; i < 4; i++)
      for(j = 0; j < 4; j++)
        m4a[k][i][j] = extreme();
  for(i = 0; i < N1; i++)
    MulVec3Mat33_F16(vout[i], vin[i], ma[i % NMAT]);
  math_line("V3XM33 MAX", crc32_update(0, (const u8 *)vout, sizeof(vout)), 0);
  raw_line("V3 MAX V0 ", vout[0]);
  for(i = 0; i < N1; i++)
    MulVec4Mat44_F16(v4out[i], v4in[i], m4a[i % NMAT]);
  math_line("V4XM44 MAX", crc32_update(0, (const u8 *)v4out, sizeof(v4out)), 0);
  for(i = 0; i < N2; i++)
    MulMat33Mat33_F16(m3out[i], ma[i % NMAT], ma[(i * 7) % NMAT]);
  math_line("M33 MAX   ", crc32_update(0, (const u8 *)m3out, sizeof(m3out)), 0);
  for(i = 0; i < N1; i++)
    fout[i] = Dot3_F16(vin[i], vin[(i + 1) % N1]);
  math_line("DOT3 MAX  ", crc32_update(0, (const u8 *)fout, sizeof(fout)), 0);
  for(i = 0; i < N1; i++)
    Cross3_F16(vout[i], vin[i], vin[(i + 1) % N1]);
  math_line("CROSS MAX ", crc32_update(0, (const u8 *)vout, sizeof(vout)), 0);
  for(i = 0; i < N1; i++)
    fout[i] = AbsVec3_F16(vin[i]);
  math_line("ABS MAX   ", crc32_update(0, (const u8 *)fout, sizeof(fout)), 0);
}
