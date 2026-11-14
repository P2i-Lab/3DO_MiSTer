/*
  P2i Lab 3DO bench: declarations shared by the test groups.
*/

#ifndef BENCH_H
#define BENCH_H

/* Texts on the screen: English, or French when BENCH_FR is defined (the language of the burned discs 2.3 to
   3.5 and of the photos of the measurements). Each English text has the length of the French one and leaves
   the compiler the same texts to share, so both programs have the same code at the same addresses and give
   the same measurements. */
#ifdef BENCH_FR
#define L(fr, en) fr
#else
#define L(fr, en) en
#endif

#include "types.h"
#include "displayutils.h"
#include "graphics.h"
#include "hardware.h"

#define SCREEN_W     320
#define SCREEN_H     240

#define PIXC_SOLID   0x1F001F00   /* source pixel only */
#define PIXC_HALF    0x1F811F81   /* (source + frame buffer) / 2 */

#define MAX_LINES    18
#define LINE_CHARS   48

#define STAT_MAX     64

/* samples of one measurement: min, median, max are shown */
typedef struct Stat
{
  u32 n;
  u32 min;
  u32 max;
  u32 v[STAT_MAX];
} Stat;

/* a time span in microseconds (vbl is not used any more) */
typedef struct Clock
{
  u32 us;
  u32 vbl;
} Clock;

/* display and timers, set up by main.c */
extern ScreenContext b_sc;
extern Item b_vbl_ioreq;
extern Item b_vram_ioreq;
extern Item b_timer_ioreq;
extern volatile u32 b_sink;

/* the result page being built */
extern char b_lines[MAX_LINES][LINE_CHARS];
extern int b_line_count;

/* util.c */
u32   usec_now(void);
u32   vbl_now(void);
void  clock_start(Clock *c);
u32   clock_us(const Clock *c);
char *put_s(char *p, const char *s);
char *put_u(char *p, u32 v, int width);
char *put_x(char *p, u32 v, int digits);
char *new_line(const char *name);
void  log_line(void);
void  text_line(const char *s);
void  progress(const char *name);
void  clear_screen(int screen);
void  draw_row(int screen, int row, const char *s, Color color);
void  show_page(const char *title, const char *head, const char *status);
void  stat_reset(Stat *s);
void  stat_add(Stat *s, u32 us);
void  stat_line(const char *name, const Stat *s);
void  value_line(const char *name, u32 us);
u32   crc32_update(u32 crc, const u8 *p, u32 n);
u32   rand_next(u32 *seed);
u32   stat_median(const Stat *s);
void  grid_begin(char tag, int cols);
void  grid_u(u32 v);
void  grid_x(u32 v, int digits);
void  grid_end(void);

/* cels.c: cel data shared by the cel groups */
#define C_FILL       0
#define C_IMAGE      1
#define C_16BPP      2
#define C_8BPP       3
#define C_HALF       4
#define C_DOT        5
#define C_SMALL      6
#define SMALL_CELS   1000

extern CCB *b_ccbs;
extern u16 *b_src16;
extern u16 *b_plut;

int  cels_setup(void);
void cels_free(void);
void put_bits(u32 *buf, u32 *pos, u32 value, u32 nbits);
void time_cels(const char *name, CCB *a, CCB *b, u32 reps);
void init_ccb(CCB *c, u32 flags, void *src, void *lut, u32 pre0, u32 pre1, u32 pixc);

/* the test groups; each fills the result page */
void group_speed(void);    /* speed.c */
void group_cels(void);     /* celtests.c */
void group_render(void);   /* celtests.c */
void group_cpu(void);      /* cpu.c */
void group_math(void);     /* math.c */
void group_hw(void);       /* hw.c */
int  group_regs(int page); /* hw.c: one page of registers, returns the page count */
void hw_close(void);       /* hw.c: closes the svc_mem device */
void group_cd(void);       /* cd.c */

/* version 3.0 */
void group_sweep(int page);  /* sweep.c: 2 pages */
void group_pixc(int page);   /* pixc.c: 2 pages */
void group_formats(void);    /* pixc.c */
void group_math2(void);      /* math.c */
void group_mem(void);        /* cpu.c */
void group_arm_time(void);   /* arm.c */
void group_arm_crc(void);    /* arm.c */
void group_dsp(void);        /* dsp.c */
void group_cd2(void);        /* cd.c */
void group_regtime(void);    /* hw.c */
void group_scan(int page);   /* hw.c: 2 pages */
void group_pbus(void);       /* hw.c */
void group_gun(void);        /* hw.c: interactive */
void group_system(void);     /* hw.c */
void group_clock(void);      /* hw.c */
void group_mire(void);       /* mire.c: interactive */
u32  wait_buttons(u32 mask); /* main.c */

/* version 3.1 */
void group_pb_live(void);    /* hw.c: interactive */
void group_video(void);      /* hw.c */
void group_vram_limit(void); /* hw.c */
void group_scan3(void);      /* hw.c: interactive */
int  gun_present(void);      /* hw.c */
void wait_release(void);     /* hw.c */
void group_pix3(int page);   /* pixc.c: 4 pages */
void group_sweep3(int page); /* sweep.c: 2 pages */
void group_cd3(void);        /* cd.c */
int  cd_stress_open(void);   /* cd.c */
int  cd_stress_read(u32 lba);
void cd_stress_close(void);
void group_arm_crc3(void);   /* arm.c: chooses its start */
void group_endurance(void);  /* endure.c */
void group_scene(void);      /* scene.c */

/* version 3.2 */
void group_arm_step(void);   /* arm.c */
void group_cd4(void);        /* cd.c */
void group_scale(void);      /* sweep.c */

/* version 3.3 */
void group_cut(void);        /* sweep.c */
void group_xy(void);         /* sweep.c */
void group_widths(int page); /* sweep.c: 3 pages */
void group_holes(void);      /* sweep.c */
void group_muls(int page);   /* arm.c: 3 pages */
void group_arm_aligned(int ask); /* arm.c */
void group_unaligned(void);  /* arm.c: asks where to start */

/* version 3.4 */
void group_acepos(void);     /* sweep.c */
void group_source(void);     /* sweep.c */
void group_packs34(int page); /* sweep.c: 2 pages */
void group_vimg(void);       /* hw.c: interactive */
void group_gun2(void);       /* hw.c: two light guns */
int  gun_count(void);        /* hw.c: 0, 1 or 2 light guns on the port */

/* version 3.5 */
void group_cdfiles(void);     /* cd.c: reads through the file system */
void group_gunsweep(int page); /* hw.c: a resting light gun, bars that move */
void group_cpumap(void);     /* hw.c: where in the field the program loses the processor */
void group_genpath(void);    /* sweep.c: general path of the corner engines */
void formats_detail(void);   /* pixc.c: RENDU DETAIL 1/2 */
void geometry_detail(void);  /* pixc.c: RENDU DETAIL 2/2, FORMATS group 12 quad by quad */
void scene_detail(void);     /* scene.c: RENDU DETAIL 2/2, the wizard scene cel by cel */

/* version 3.6 */
extern u32 b_sync;            /* util.c: 1 = each timed draw starts right after a VBL */
extern u32 b_spread_max;      /* util.c: the largest max - min of a measure on the page */
extern u32 b_spread_n;        /* util.c: measures spread over 64 us */
extern u32 b_jumps;           /* util.c: measures with a clock jump */
void sync_reset(void);        /* util.c */
void sync_note(const Stat *s); /* util.c */
void sync_head(char *buf);    /* util.c: the spread line of a synchronised page */
void group_phase(void);       /* sweep.c: one list started at 16 points of the field */
void group_shear(void);       /* sweep.c: sheared rows and perspective */
void group_rot(void);         /* sweep.c: rotations */
void group_acesc(void);       /* sweep.c: ACE at vertical scales 1 to 2, X by Y */
void group_narrow(void);      /* sweep.c: narrow ACE cels, source alignment */
void group_fade(void);        /* sweep.c: fills and blends */
void group_off(void);         /* sweep.c: cels partly or fully off the screen */
void group_trf(void);         /* sweep.c: transparent layouts at fine scales */
void scene_bands(void);       /* scene.c: wizard scene hashes by band */
void scene_adds(void);        /* scene.c: additions over known pixels */

#endif
