/*
  P2i Lab 3DO bench, group CD DETAIL: the drive read directly through the CD-ROM
  device, by block number. Disc size and table of contents, sequential reads at
  single and double speed, one-block reads, the same block again, and seeks of
  growing length (each line: time per read, min / average / max, in microseconds).
  The disc carries a padding file (bench_data/pad.bin, each block starting with
  "P2PD" and its index) so that long seeks stay on the disc.
*/

#include "bench.h"

#include "cdrom.h"
#include "filefunctions.h"
#include "filesystem.h"
#include "device.h"
#include "driver.h"
#include "io.h"
#include "item.h"
#include "mem.h"
#include "string.h"

#define BLOCK        2048
#define CHUNK        16          /* blocks per read in the sequential tests */
#define SEQ_BLOCKS   256         /* 512 KB */
#define SEEK_BASE    1000
#define SEEK_READS   16

static Item cd_dev = -1;
static Item cd_ior = -1;
static u8 *cd_buf;
static DeviceStatus cd_status;
static struct CDROM_Disc_Data cd_disc;

static Err cd_io(u8 command, u32 lba, void *buf, u32 len, u32 speed)
{
  IOInfo ioi;
  union CDROMCommandOptions opt;

  memset(&ioi, 0, sizeof(ioi));
  opt.asLongword = 0;
  opt.asFields.speed = speed;
  ioi.ioi_Command = command;
  ioi.ioi_CmdOptions = opt.asLongword;
  ioi.ioi_Offset = (s32)lba;
  ioi.ioi_Recv.iob_Buffer = buf;
  ioi.ioi_Recv.iob_Len = (s32)len;
  return DoIO(cd_ior, &ioi);
}

static Err cd_read(u32 lba, u32 blocks, u32 speed)
{
  return cd_io(CMD_READ, lba, cd_buf, blocks * BLOCK, speed);
}

static void err_line(const char *name, Err err)
{
  char *p;

  p = new_line(name);
  p = put_s(p, L("ERREUR ", "ERROR  "));
  p = put_x(p, (u32)err, 8);
  (void)p;
  log_line();
}

static void cd_close(void)
{
  if(cd_ior >= 0)
    DeleteIOReq(cd_ior);
  if(cd_dev >= 0)
    CloseNamedDevice(cd_dev);
  if(cd_buf != NULL)
    FreeMem(cd_buf, CHUNK * BLOCK);
  cd_ior = -1;
  cd_dev = -1;
  cd_buf = NULL;
}

static void info_lines(void)
{
  Err err;
  char *p;

  memset(&cd_status, 0, sizeof(cd_status));
  err = cd_io(CMD_STATUS, 0, &cd_status, sizeof(cd_status), 0);
  if(err < 0)
    err_line(L("STATUT ", "STATUS "), err);
  p = new_line(L("BLOCS     ", "BLOCKS    "));
  p = put_u(p, cd_status.ds_DeviceBlockCount, 7);
  p = put_s(p, " FL ");
  p = put_x(p, cd_status.ds_DeviceFlagWord, 8);
  (void)p;
  log_line();

  memset(&cd_disc, 0, sizeof(cd_disc));
  err = cd_io(CDROMCMD_DISCDATA, 0, &cd_disc, sizeof(cd_disc), 0);
  if(err < 0)
    err_line("TOC ", err);
  p = new_line(L("PISTES    ", "TRACKS    "));
  p = put_u(p, cd_disc.info.firstTrackNumber, 0);
  p = put_s(p, "-");
  p = put_u(p, cd_disc.info.lastTrackNumber, 0);
  p = put_s(p, L(" FIN ", " END "));
  p = put_u(p, cd_disc.info.minutes, 2);
  p = put_s(p, ":");
  p = put_u(p, cd_disc.info.seconds, 2);
  p = put_s(p, ":");
  p = put_u(p, cd_disc.info.frames, 2);
  p = put_s(p, " ID ");
  p = put_x(p, cd_disc.info.discID, 2);
  (void)p;
  log_line();

  p = new_line("SESSION   ");
  p = put_u(p, cd_disc.session.valid, 0);
  p = put_s(p, " ");
  p = put_u(p, cd_disc.session.minutes, 2);
  p = put_s(p, ":");
  p = put_u(p, cd_disc.session.seconds, 2);
  p = put_s(p, ":");
  p = put_u(p, cd_disc.session.frames, 2);
  p = put_s(p, " P1 ");
  p = put_x(p, cd_disc.TOC[1].addressAndControl, 2);
  (void)p;
  log_line();
}

/* SEQ_BLOCKS blocks read CHUNK at a time, after one read to settle the speed */
static void seq_line(const char *name, u32 lba, u32 speed)
{
  u32 i;
  Clock ck;
  u32 t;
  Err err;

  progress(name);
  err = cd_read(lba - 1, 1, speed);
  clock_start(&ck);
  for(i = 0; i < SEQ_BLOCKS && err >= 0; i += CHUNK)
    err = cd_read(lba + i, CHUNK, speed);
  t = clock_us(&ck);
  if(err < 0)
    err_line(name, err);
  else
    value_line(name, t);
}

/* reads alternating between a and b, one block each: every read is a seek */
static void seek_line(const char *name, u32 a, u32 b, u32 speed)
{
  static Stat s;
  u32 i;
  u32 t0;
  Err err;

  progress(name);
  stat_reset(&s);
  err = cd_read(a, 1, speed);
  for(i = 0; i < SEEK_READS && err >= 0; i++)
    {
      t0 = usec_now();
      err = cd_read((i & 1) ? a : b, 1, speed);
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(name, err);
  else
    stat_line(name, &s);
}

static void pad_line(u32 lba)
{
  Err err;
  char *p;

  progress("PAD");
  err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
  if(err < 0)
    {
      err_line("PAD ", err);
      return;
    }
  p = new_line("PAD ");
  p = put_u(p, lba, 6);
  p = put_s(p, " ");
  p = put_x(p, ((u32)cd_buf[0] << 24) | ((u32)cd_buf[1] << 16) | ((u32)cd_buf[2] << 8) | cd_buf[3], 8);
  p = put_s(p, " ");
  p = put_x(p, ((u32)cd_buf[4] << 24) | ((u32)cd_buf[5] << 16) | ((u32)cd_buf[6] << 8) | cd_buf[7], 8);
  (void)p;
  log_line();
}

void group_cd(void)
{
  static Stat s;
  u32 i;
  u32 t0;
  u32 blocks;
  u32 far;
  Err err;

  progress(L("OUVERTURE CD-ROM", "OPENING CD-ROM  "));
  cd_buf = (u8 *)AllocMem(CHUNK * BLOCK, MEMTYPE_DRAM | MEMTYPE_DMA);
  cd_dev = OpenNamedDevice("CD-ROM", 0);
  if(cd_dev >= 0)
    cd_ior = CreateIOReq(NULL, 0, cd_dev, 0);
  if(cd_buf == NULL || cd_dev < 0 || cd_ior < 0)
    {
      err_line("CD-ROM ", (cd_dev < 0) ? cd_dev : cd_ior);
      cd_close();
      return;
    }

  progress(L("STATUT ET TOC", "STATUS + TOC "));
  info_lines();
  blocks = cd_status.ds_DeviceBlockCount;

  seq_line("SEQ 1X 512", 2001, CDROM_SINGLE_SPEED);
  seq_line("SEQ 2X 512", 2001, CDROM_DOUBLE_SPEED);

  progress(L("1 BLOC 2X", "1 BLK 2X "));
  stat_reset(&s);
  err = 0;
  for(i = 0; i < 64 && err >= 0; i++)
    {
      t0 = usec_now();
      err = cd_read(3000 + i, 1, CDROM_DOUBLE_SPEED);
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(L("1 BLOC ", "1 BLK  "), err);
  else
    stat_line(L("1 BLOC 2X ", "1 BLOCK 2X"), &s);

  seek_line(L("MEME BLOC ", "SAME BLOCK"), 3000, 3000, CDROM_DOUBLE_SPEED);
  seek_line(L("SAUT 16   ", "JUMP 16   "), SEEK_BASE, SEEK_BASE + 16, CDROM_DOUBLE_SPEED);
  seek_line(L("SAUT 256  ", "JUMP 256  "), SEEK_BASE, SEEK_BASE + 256, CDROM_DOUBLE_SPEED);
  seek_line(L("SAUT 4096 ", "JUMP 4096 "), SEEK_BASE, SEEK_BASE + 4096, CDROM_DOUBLE_SPEED);
  seek_line(L("SAUT 4K 1X", "JUMP 4K 1X"), SEEK_BASE, SEEK_BASE + 4096, CDROM_SINGLE_SPEED);
  if(blocks > SEEK_BASE + 32768 + 100)
    seek_line(L("SAUT 32K  ", "JUMP 32K  "), SEEK_BASE, SEEK_BASE + 32768, CDROM_DOUBLE_SPEED);
  else
    text_line(L("SAUT 32K: DISQUE COURT", "JUMP 32K: SHORT DISC  "));
  far = (blocks > SEEK_BASE + 200) ? blocks - 100 : SEEK_BASE + 100;
  seek_line(L("SAUT MAX  ", "JUMP MAX  "), SEEK_BASE, far, CDROM_DOUBLE_SPEED);
  pad_line(blocks / 2);

  cd_close();
}

/* ---------------------------------------------------------------- version 3.0 */

#define GAP_BASE     5000
#define SAMPLES_CD   16

static void cd_cell(Err err, const Stat *s)
{
  if(err < 0)
    grid_x((u32)err, 6);
  else
    grid_u(stat_median(s));
}

/* a read of one block, then the next one gap blocks further on */
static void gap_cell(u32 gap, u32 speed)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = GAP_BASE;
  stat_reset(&s);
  err = cd_read(lba, 1, speed);
  for(i = 0; i < SAMPLES_CD && err >= 0; i++)
    {
      lba += 1 + gap;
      t0 = usec_now();
      err = cd_read(lba, 1, speed);
      stat_add(&s, usec_now() - t0);
    }
  cd_cell(err, &s);
}

/* idle_ms of waiting before each read of the next block */
static void idle_cell(u32 idle_ms)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = GAP_BASE + 2000;
  stat_reset(&s);
  err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
  for(i = 0; i < 6 && err >= 0; i++)
    {
      t0 = usec_now();
      while(usec_now() - t0 < idle_ms * 1000)
        ;
      lba++;
      t0 = usec_now();
      err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
      stat_add(&s, usec_now() - t0);
    }
  cd_cell(err, &s);
}

/* n blocks per request, sequentially */
static void size_cell(u32 blocks)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = GAP_BASE + 4000;
  stat_reset(&s);
  err = cd_read(lba, blocks, CDROM_DOUBLE_SPEED);
  for(i = 0; i < SAMPLES_CD && err >= 0; i++)
    {
      lba += blocks;
      t0 = usec_now();
      err = cd_read(lba, blocks, CDROM_DOUBLE_SPEED);
      stat_add(&s, usec_now() - t0);
    }
  cd_cell(err, &s);
}

/* a read at 1x followed by a read at 2x */
static void switch_cell(void)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = GAP_BASE + 8000;
  stat_reset(&s);
  err = 0;
  for(i = 0; i < 8 && err >= 0; i++)
    {
      err = cd_read(lba, 1, CDROM_SINGLE_SPEED);
      lba++;
      if(err >= 0)
        {
          t0 = usec_now();
          err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
          stat_add(&s, usec_now() - t0);
          lba++;
        }
    }
  cd_cell(err, &s);
}

/* pitch and read-ahead options: the time of 64 blocks */
static void mode_cell(u32 pitch, u32 ahead)
{
  IOInfo ioi;
  union CDROMCommandOptions opt;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = GAP_BASE + 10000;
  (void)cd_read(lba - 1, 1, CDROM_DOUBLE_SPEED);   /* version 3.2: not the jump to this place */
  t0 = usec_now();
  err = 0;
  for(i = 0; i < 4 && err >= 0; i++)
    {
      memset(&ioi, 0, sizeof(ioi));
      opt.asLongword = 0;
      opt.asFields.speed = CDROM_DOUBLE_SPEED;
      opt.asFields.pitch = pitch;
      opt.asFields.readAhead = ahead;
      ioi.ioi_Command = CMD_READ;
      ioi.ioi_CmdOptions = opt.asLongword;
      ioi.ioi_Offset = (s32)lba;
      ioi.ioi_Recv.iob_Buffer = cd_buf;
      ioi.ioi_Recv.iob_Len = CHUNK * BLOCK;
      err = DoIO(cd_ior, &ioi);
      lba += CHUNK;
    }
  if(err < 0)
    grid_x((u32)err, 6);
  else
    grid_u(usec_now() - t0);
}

static void raw_block(const char *name, u32 length, u32 density)
{
  IOInfo ioi;
  union CDROMCommandOptions opt;
  Err err;
  char *p;
  u32 i;

  memset(&ioi, 0, sizeof(ioi));
  memset(cd_buf, 0, CHUNK * BLOCK);
  opt.asLongword = 0;
  opt.asFields.speed = CDROM_DOUBLE_SPEED;
  opt.asFields.blockLength = length;
  opt.asFields.densityCode = density;
  ioi.ioi_Command = CMD_READ;
  ioi.ioi_CmdOptions = opt.asLongword;
  ioi.ioi_Offset = GAP_BASE + 12000;
  ioi.ioi_Recv.iob_Buffer = cd_buf;
  ioi.ioi_Recv.iob_Len = (s32)length;
  err = DoIO(cd_ior, &ioi);
  p = new_line(name);
  if(err < 0)
    {
      p = put_s(p, L("ERREUR ", "ERROR  "));
      p = put_x(p, (u32)err, 8);
    }
  else
    {
      p = put_x(p, crc32_update(0, cd_buf, length), 8);
      p = put_s(p, " ");
      for(i = 0; i < 8; i++)
        p = put_x(p, cd_buf[i + ((length == 2048) ? 0 : 12)], 2);
    }
  (void)p;
  log_line();
}

static void subq_line(void)
{
  IOInfo ioi;
  Err err;
  char *p;
  u32 i;

  (void)cd_read(GAP_BASE + 14000, 1, CDROM_DOUBLE_SPEED);
  memset(&ioi, 0, sizeof(ioi));
  memset(cd_buf, 0, 32);
  ioi.ioi_Command = CDROMCMD_READ_SUBQ;
  ioi.ioi_Recv.iob_Buffer = cd_buf;
  ioi.ioi_Recv.iob_Len = 16;
  err = DoIO(cd_ior, &ioi);
  p = new_line("SUBQ ");
  if(err < 0)
    {
      p = put_s(p, L("ERREUR ", "ERROR  "));
      p = put_x(p, (u32)err, 8);
    }
  else
    for(i = 0; i < 12; i++)
      p = put_x(p, cd_buf[i], 2);
  (void)p;
  log_line();
}

void group_cd2(void)
{
  static const u16 gaps[10] = { 1, 2, 3, 4, 6, 8, 12, 16, 32, 64 };
  static const u16 idles[5] = { 20, 100, 500, 1000, 2000 };
  static const u16 sizes[6] = { 1, 2, 4, 8, 12, 16 };
  u32 i;

  progress(L("OUVERTURE CD-ROM", "OPENING CD-ROM  "));
  cd_buf = (u8 *)AllocMem(CHUNK * BLOCK, MEMTYPE_DRAM | MEMTYPE_DMA);
  cd_dev = OpenNamedDevice("CD-ROM", 0);
  if(cd_dev >= 0)
    cd_ior = CreateIOReq(NULL, 0, cd_dev, 0);
  if(cd_buf == NULL || cd_dev < 0 || cd_ior < 0)
    {
      err_line("CD-ROM ", (cd_dev < 0) ? cd_dev : cd_ior);
      cd_close();
      return;
    }

  grid_begin('Z', 5);
  progress(L("AVANCES 2X", "STEPS 2X  "));
  for(i = 0; i < 10; i++)
    gap_cell(gaps[i], CDROM_DOUBLE_SPEED);
  progress(L("AVANCES 1X", "STEPS 1X  "));
  for(i = 0; i < 5; i++)
    gap_cell(gaps[i], CDROM_SINGLE_SPEED);
  progress(L("ATTENTES", "WAITS   "));
  for(i = 0; i < 5; i++)
    idle_cell(idles[i]);
  progress(L("TAILLES", "SIZES  "));
  for(i = 0; i < 6; i++)
    size_cell(sizes[i]);
  progress(L("CHANGEMENT DE VITESSE", "CHANGE OF SPEED      "));
  switch_cell();
  progress("MODES");
  mode_cell(CDROM_PITCH_SLOW, 0);
  mode_cell(CDROM_PITCH_NORMAL, 0);
  mode_cell(CDROM_PITCH_FAST, 0);
  mode_cell(0, CDROM_READAHEAD_ENABLED);
  mode_cell(0, CDROM_READAHEAD_DISABLED);
  grid_end();
  progress(L("BLOCS BRUTS", "RAW BLOCKS "));
  raw_block("B2048 ", 2048, 0);
  raw_block("B2352 ", CDROM_DA, 0);
  raw_block("D2352 ", CDROM_DA, CDROM_DIGITAL_AUDIO);
  subq_line();
  cd_close();
}

/* ---------------------------------------------------------------- version 3.1 */

#define CD3_SAMPLES  8

/* a read, then reads step blocks further on (step may be zero or negative) */
static void step_cell(u32 base, s32 step)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = base;
  stat_reset(&s);
  err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
  for(i = 0; i < CD3_SAMPLES && err >= 0; i++)
    {
      lba = (u32)((s32)lba + step);
      t0 = usec_now();
      err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
      stat_add(&s, usec_now() - t0);
    }
  cd_cell(err, &s);
}

/* one long jump, forward only: the way back is not timed */
static void long_cell(u32 from, u32 dist)
{
  static Stat s;
  u32 i;
  u32 t0;
  Err err;

  stat_reset(&s);
  err = 0;
  for(i = 0; i < 6 && err >= 0; i++)
    {
      err = cd_read(from + i, 1, CDROM_DOUBLE_SPEED);
      if(err >= 0)
        {
          t0 = usec_now();
          err = cd_read(from + i + dist, 1, CDROM_DOUBLE_SPEED);
          stat_add(&s, usec_now() - t0);
        }
    }
  cd_cell(err, &s);
}

/* two seconds after a read, block n further on: what the drive read ahead */
static void ahead_cell(u32 n)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = 30000 + n * 100;
  stat_reset(&s);
  err = 0;
  for(i = 0; i < 3 && err >= 0; i++)
    {
      err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
      t0 = usec_now();
      while(usec_now() - t0 < 2000000)
        ;
      if(err >= 0)
        {
          t0 = usec_now();
          err = cd_read(lba + n, 1, CDROM_DOUBLE_SPEED);
          stat_add(&s, usec_now() - t0);
        }
      lba += 512;
    }
  cd_cell(err, &s);
}

/* a read at one speed followed by the next block at the other */
static void speed_cell(u32 first, u32 second, u32 base)
{
  static Stat s;
  u32 i;
  u32 lba;
  u32 t0;
  Err err;

  lba = base;
  stat_reset(&s);
  err = 0;
  for(i = 0; i < 6 && err >= 0; i++)
    {
      err = cd_read(lba, 1, first);
      lba++;
      if(err >= 0)
        {
          t0 = usec_now();
          err = cd_read(lba, 1, second);
          stat_add(&s, usec_now() - t0);
          lba++;
        }
    }
  cd_cell(err, &s);
}

static int cd3_open(void)
{
  cd_buf = (u8 *)AllocMem(CHUNK * BLOCK, MEMTYPE_DRAM | MEMTYPE_DMA);
  cd_dev = OpenNamedDevice("CD-ROM", 0);
  if(cd_dev >= 0)
    cd_ior = CreateIOReq(NULL, 0, cd_dev, 0);
  return (cd_buf != NULL && cd_dev >= 0 && cd_ior >= 0) ? 0 : -1;
}

/* ---------------------------------------------------------------- version 3.2 */

/* 64 consecutive blocks with a speed option (0 = none, what games ask), and the state byte of the drive afterwards */
static void speed4_line(const char *name, u32 speed)
{
  IOInfo ioi;
  u32 i;
  u32 lba;
  u32 t;
  Err err;
  char *p;

  lba = GAP_BASE + 20000;
  err = cd_read(lba - 1, 1, speed);
  t = usec_now();
  for(i = 0; i < 64 && err >= 0; i++)
    err = cd_read(lba + i, 1, speed);
  t = usec_now() - t;
  memset(&ioi, 0, sizeof(ioi));
  memset(cd_buf, 0, 32);
  ioi.ioi_Command = CDROMCMD_READ_SUBQ;
  ioi.ioi_Recv.iob_Buffer = cd_buf;
  ioi.ioi_Recv.iob_Len = 16;
  if(err >= 0)
    err = DoIO(cd_ior, &ioi);
  p = new_line(name);
  if(err < 0)
    {
      p = put_s(p, L("ERREUR ", "ERROR  "));
      p = put_x(p, (u32)err, 8);
    }
  else
    {
      p = put_u(p, t, 8);
      p = put_s(p, L(" US  ETAT ", " US  STATE"));
      p = put_x(p, cd_buf[11], 2);
    }
  (void)p;
  log_line();
}

void group_cd4(void)
{
  progress(L("OUVERTURE CD-ROM", "OPENING CD-ROM  "));
  if(cd3_open() < 0)
    {
      err_line("CD-ROM ", (cd_dev < 0) ? cd_dev : cd_ior);
      cd_close();
      return;
    }
  progress(L("SANS OPTION", "NO OPTION  "));
  speed4_line(L("DEFAUT ", "DEFAULT"), 0);
  progress("1X");
  speed4_line("1X     ", CDROM_SINGLE_SPEED);
  progress(L("DEFAUT APRES 1X", "DEFAULT AFT. 1X"));
  speed4_line(L("DEFAUT ", "DEFAULT"), 0);
  progress("2X");
  speed4_line("2X     ", CDROM_DOUBLE_SPEED);
  progress(L("DEFAUT APRES 2X", "DEFAULT AFT. 2X"));
  speed4_line(L("DEFAUT ", "DEFAULT"), 0);
  cd_close();
}

void group_cd3(void)
{
  static const u16 gaps[10]  = { 1, 2, 4, 5, 9, 13, 17, 33, 65, 129 };
  static const s16 backs[6]  = { 0, -1, -2, -8, -64, -1024 };
  static const u16 bases[3]  = { 5000, 35000, 65000 };
  static const u16 bbases[3] = { 14000, 40000, 65000 };
  static const u16 dists[6]  = { 256, 1024, 4096, 16384, 32768, 60000 };
  static const u8  aheads[6] = { 1, 2, 4, 8, 16, 32 };
  u32 b;
  u32 i;

  progress(L("OUVERTURE CD-ROM", "OPENING CD-ROM  "));
  if(cd3_open() < 0)
    {
      err_line("CD-ROM ", (cd_dev < 0) ? cd_dev : cd_ior);
      cd_close();
      return;
    }
  grid_begin('Y', 4);
  progress(L("AVANCES", "STEPS  "));
  for(b = 0; b < 3; b++)
    for(i = 0; i < 10; i++)
      step_cell(bases[b], (s32)gaps[i]);
  progress(L("RETOURS", "BACK   "));
  for(b = 0; b < 3; b++)
    for(i = 0; i < 6; i++)
      step_cell(bbases[b], (s32)backs[i]);
  progress(L("LONGS SAUTS", "LONG JUMPS "));
  for(i = 0; i < 6; i++)
    long_cell(2000, dists[i]);
  progress(L("LECTURE ANTICIPEE", "READ-AHEAD       "));
  for(i = 0; i < 6; i++)
    ahead_cell(aheads[i]);
  progress(L("VITESSES", "SPEEDS  "));
  speed_cell(CDROM_SINGLE_SPEED, CDROM_DOUBLE_SPEED, 9000);
  speed_cell(CDROM_DOUBLE_SPEED, CDROM_SINGLE_SPEED, 9500);
  grid_end();
  cd_close();
}

/* for the endurance page: reads a block of the padding file and checks its header */
static u32 stress_delta;
static int stress_known;

int cd_stress_open(void)
{
  stress_known = 0;
  return cd3_open();
}

/* 0 = good, 1 = wrong contents, negative = error of the device */
int cd_stress_read(u32 lba)
{
  Err err;
  u32 *w;

  err = cd_read(lba, 1, CDROM_DOUBLE_SPEED);
  if(err < 0)
    return (int)err;
  w = (u32 *)cd_buf;
  if(w[0] != 0x50325044u)
    return 1;
  if(!stress_known)
    {
      stress_delta = lba - w[1];
      stress_known = 1;
    }
  return (lba - w[1] == stress_delta) ? 0 : 1;
}

void cd_stress_close(void)
{
  cd_close();
}

/* ---------------------------------------------------------------- version 3.5 */

/* CD FICHIERS (3.5): reads through the file system, as the OS and the games load their files (the boot of Mad Dog McCree
   is about 500 one-block reads; the core takes about twice as long as a real 3DO). The CD pages above read the drive
   directly. Lines: MIN MED MAX in microseconds, or the total time. */
#define CDF_SMALL  "bench_data/cd512k.bin"
#define CDF_BIG    "bench_data/pad.bin"
#define CDF_BLOCKS 32

static Item cdf_file = -1;
static Item cdf_ior = -1;
static u8 *cdf_buf;

static void cdf_close(void)
{
  if(cdf_ior >= 0)
    DeleteIOReq(cdf_ior);
  if(cdf_file >= 0)
    CloseDiskFile(cdf_file);
  cdf_ior = -1;
  cdf_file = -1;
}

static Err cdf_open(const char *path)
{
  cdf_close();
  cdf_file = OpenDiskFile((char *)path);
  if(cdf_file < 0)
    return cdf_file;
  cdf_ior = CreateIOReq(NULL, 0, cdf_file, 0);
  return cdf_ior;
}

static Err cdf_io(s32 command, u32 block, void *buf, u32 len)
{
  IOInfo ioi;

  memset(&ioi, 0, sizeof(ioi));
  ioi.ioi_Command = (u8)command;
  ioi.ioi_Offset = (s32)block;
  ioi.ioi_Recv.iob_Buffer = buf;
  ioi.ioi_Recv.iob_Len = (s32)len;
  return DoIO(cdf_ior, &ioi);
}

void group_cdfiles(void)
{
  static Stat s;
  static FileStatus fst;
  u32 i;
  u32 t0;
  Clock ck;
  Err err;
  Item it;
  char *p;

  cdf_buf = (u8 *)AllocMem(CDF_BLOCKS * BLOCK, MEMTYPE_DRAM | MEMTYPE_DMA);
  if(cdf_buf == NULL)
    {
      text_line(L("CD FICHIERS: PAS DE MEMOIRE", "CD FILES: OUT OF MEMORY    "));
      return;
    }

  /* opening a file: the first time the directory blocks may be read */
  progress(L("OUVRIR", "OPEN  "));
  stat_reset(&s);
  err = 0;
  for(i = 0; i < 16; i++)
    {
      t0 = usec_now();
      it = OpenDiskFile((char *)((i & 1) ? CDF_BIG : CDF_SMALL));
      stat_add(&s, usec_now() - t0);
      if(it < 0)
        {
          err = it;
          break;
        }
      CloseDiskFile(it);
    }
  if(err < 0)
    err_line(L("OUVRIR ", "OPEN   "), err);
  else
    stat_line(L("OUVRIR    ", "OPEN      "), &s);

  err = cdf_open(CDF_SMALL);
  if(err < 0)
    {
      err_line("CD512K ", err);
      goto done;
    }

  /* an I/O through the file system that does not touch the disc */
  progress(L("STATUT", "STATUS"));
  stat_reset(&s);
  for(i = 0; i < 32 && err >= 0; i++)
    {
      t0 = usec_now();
      err = cdf_io(CMD_STATUS, 0, &fst, sizeof(fst));
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(L("STATUT ", "STATUS "), err);
  else
    stat_line(L("STATUT    ", "STATUS    "), &s);

  /* one block at a time, in a row, as the boot */
  progress(L("L1 SUITE", "L1 NEXT "));
  if(err >= 0)
    err = cdf_io(CMD_READ, 0, cdf_buf, BLOCK);
  stat_reset(&s);
  for(i = 1; i <= 64 && err >= 0; i++)
    {
      t0 = usec_now();
      err = cdf_io(CMD_READ, i, cdf_buf, BLOCK);
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(L("L1 SUITE ", "L1 NEXT  "), err);
  else
    stat_line(L("L1 SUITE  ", "L1 NEXT   "), &s);

  /* one block at a time, one field apart */
  progress(L("L1 ESPACE", "L1 SPACED"));
  stat_reset(&s);
  for(i = 100; i < 132 && err >= 0; i++)
    {
      WaitVBL(b_vbl_ioreq, 1);
      t0 = usec_now();
      err = cdf_io(CMD_READ, i, cdf_buf, BLOCK);
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(L("L1 ESPACE ", "L1 SPACED "), err);
  else
    stat_line(L("L1 ESPACE ", "L1 SPACED "), &s);

  /* the whole small file, 8 then 32 blocks at a time */
  progress("L8");
  clock_start(&ck);
  for(i = 0; i < 256 && err >= 0; i += 8)
    err = cdf_io(CMD_READ, i, cdf_buf, 8 * BLOCK);
  if(err < 0)
    err_line("L8 ", err);
  else
    value_line("L8 512K   ", clock_us(&ck));
  progress("L32");
  clock_start(&ck);
  for(i = 0; i < 256 && err >= 0; i += CDF_BLOCKS)
    err = cdf_io(CMD_READ, i, cdf_buf, CDF_BLOCKS * BLOCK);
  if(err < 0)
    err_line("L32 ", err);
  else
    value_line("L32 512K  ", clock_us(&ck));

  /* one block at a time, 2000 blocks apart, in the big padding file */
  err = cdf_open(CDF_BIG);
  if(err < 0)
    {
      err_line("PAD.BIN ", err);
      goto done;
    }
  progress(L("L1 SAUT 2K", "L1 JUMP 2K"));
  err = cdf_io(CMD_READ, 0, cdf_buf, BLOCK);
  stat_reset(&s);
  for(i = 1; i <= 32 && err >= 0; i++)
    {
      t0 = usec_now();
      err = cdf_io(CMD_READ, i * 2000, cdf_buf, BLOCK);
      stat_add(&s, usec_now() - t0);
    }
  if(err < 0)
    err_line(L("L1 SAUT ", "L1 JUMP "), err);
  else
    stat_line(L("L1 SAUT 2K", "L1 JUMP 2K"), &s);

  /* the file's blocks are the right ones: block 64000 starts with "P2PD" and its index */
  if(err >= 0)
    err = cdf_io(CMD_READ, 64000, cdf_buf, BLOCK);
  if(err < 0)
    err_line("PAD 64000 ", err);
  else
    {
      p = new_line("PAD 64000 ");
      p = put_x(p, ((u32)cdf_buf[0] << 24) | ((u32)cdf_buf[1] << 16) | ((u32)cdf_buf[2] << 8) | cdf_buf[3], 8);
      p = put_s(p, " ");
      p = put_x(p, ((u32)cdf_buf[4] << 24) | ((u32)cdf_buf[5] << 16) | ((u32)cdf_buf[6] << 8) | cdf_buf[7], 8);
      (void)p;
      log_line();
    }

done:
  cdf_close();
  FreeMem(cdf_buf, CDF_BLOCKS * BLOCK);
  cdf_buf = NULL;
}
