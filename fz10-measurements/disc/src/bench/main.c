/*
  P2i Lab 3DO bench, version 3.6 (2026-11-13)

  Measures the console it runs on and shows the results as pages of text, so that a
  photograph of a real console can be compared with emulators and with the MiSTer
  core. A menu lists the groups of tests; TOUT 3.0 runs the groups new in this version,
  one page after the other, and the groups of version 2.3 follow in the list.

  Times are microseconds read with SampleSystemTimeTV(); VBL counts come from the
  timer device. Up/Down choose, A or Start runs, X goes back (or quits from the menu).
*/

#include "bench.h"

#include "audio.h"
#include "controlpad.h"
#include "debug.h"
#include "event.h"
#include "io.h"
#include "operamath.h"
#include "operror.h"
#include "string.h"
#include "timerutils.h"

ScreenContext b_sc;
Item b_vbl_ioreq;
Item b_vram_ioreq;
Item b_timer_ioreq;
volatile u32 b_sink;

#define G_ALL     0
#define G_PBL     1
#define G_GUN     2
#define G_PIX3    3
#define G_SW3     4
#define G_SCN     5
#define G_CD3     6
#define G_VID     7
#define G_VLIM    8
#define G_SC3     9
#define G_ARM3    10
#define G_END     11
#define G_SWEEP   12
#define G_PIXC    13
#define G_FORMAT  14
#define G_MATH2   15
#define G_MEM     16
#define G_ARMT    17
#define G_ARMC    18
#define G_DSP     19
#define G_CD2     20
#define G_REGT    21
#define G_SCAN    22
#define G_SYS     23
#define G_PBUS    24
#define G_CLOCK   25
#define G_MIRE    26
#define G_SPEED   27
#define G_CELS    28
#define G_CPU     29
#define G_MATH    30
#define G_RENDER  31
#define G_HW      32
#define G_CD      33
#define G_REGS    34
#define G_ARM4    35
#define G_CD4     36
#define G_SCALE   37
#define G_NA      38
#define G_ARA     39
#define G_MULC    40
#define G_HOLE    41
#define G_WID     42
#define G_XY      43
#define G_CUT     44
#define G_ALL33   45
#define G_ACEP    46
#define G_SRC     47
#define G_PK34    48
#define G_VIMG    49
#define G_GUN2    50
#define G_ALL34   51
#define G_CPUMAP  52
#define G_GENP    53
#define G_RDET    54
#define G_GUNSW   55
#define G_CDF     56
#define G_ALL35   57
#define G_PHASE   58
#define G_SHEAR   59
#define G_ROT     60
#define G_ACESC   61
#define G_NARROW  62
#define G_FADE    63
#define G_OFF     64
#define G_TRF     65
#define G_SBAND   66
#define G_ALL36A  67
#define G_ALL36B  68
#define G_COUNT   69

#define VISIBLE    18

static const char *titles[G_COUNT] =
  {
    L("TOUT 3.0 (A = PAGE SUIVANTE)", "ALL 3.0 (A = NEXT PAGE)     "),
    L("PORT MANETTE DIRECT", "CONTROL PORT LIVE  "),
    L("PISTOLET CIBLES", "GUN TARGETS    "),
    L("PIXELS EN CLAIR", "PIXEL VALUES   "),
    L("CELS ACE ETROITS", "NARROW ACE CELS "),
    L("SCENE MAGICIEN", "WIZARD SCENE  "),
    L("CD SAUTS", "CD JUMPS"),
    L("VIDEO LIGNES", "VIDEO LINES "),
    L("LIMITE VRAM", "VRAM LIMIT "),
    L("REG SCAN MANUEL", "REG SCAN MANUAL"),
    L("ARM CRC SUIVI", "ARM CRC TRACE"),
    "ENDURANCE 5 MIN",
    L("CELS BALAYAGE", "CELS SWEEP   "),
    "CELS PIXC",
    L("FORMATS GEOMETRIE", "FORMATS GEOMETRY "),
    "MATH EXTREMES",
    L("MEMOIRE PAS", "MEM STRIDES"),
    L("ARM TEMPS", "ARM TIMES"),
    "ARM CRC",
    "DSP",
    L("CD AVANCE", "CD STEPS "),
    L("REG TEMPS", "REG TIMES"),
    "REG SCAN",
    L("SYSTEME", "SYSTEM "),
    L("PORT MANETTE", "CONTROL PORT"),
    L("HORLOGES 60 S", "CLOCKS 60 S  "),
    L("MIRE VIDEO", "TEST CARD "),
    L("2.3 VITESSE", "2.3 SPEED  "),
    "2.3 CELS DETAIL",
    "2.3 CPU DETAIL",
    "2.3 MATH MADAM",
    L("2.3 RENDU CRC", "2.3 DRAW CRC "),
    L("2.3 MATERIEL", "2.3 HARDWARE"),
    "2.3 CD DETAIL",
    L("2.3 REGISTRES", "2.3 REGISTERS"),
    L("ARM PAS A PAS", "ARM STEPWISE "),
    L("CD VITESSE", "CD SPEED  "),
    L("CELS ECHELLE", "CELS SCALE  "),
    L("ARM NON ALIGNE", "ARM UNALIGNED "),
    L("ARM CRC ALIGNE", "ARM CRC ALIGN "),
    L("MULS DRAPEAU C", "MULS C FLAG   "),
    L("CELS TRANSPARENTS", "CELS TRANSPARENT "),
    L("CELS LARGEURS", "CELS WIDTHS  "),
    L("CELS ECHELLE XY", "CELS SCALE XY  "),
    L("CELS COUPES", "CELS CLIPS "),
    L("TOUT 3.3 (A = PAGE SUIVANTE)", "ALL 3.3 (A = NEXT PAGE)     "),
    "CELS ACE POSITION",
    "CELS SOURCE",
    L("CELS PAQUETS ACE", "CELS ACE PACKETS"),
    "VCNT IMAGE",
    L("PISTOLETS 2 JOUEURS", "GUNS 2 PLAYERS     "),
    L("TOUT 3.4 (A = PAGE SUIVANTE)", "ALL 3.4 (A = NEXT PAGE)     "),
    L("CARTE CPU", "CPU MAP  "),
    L("CELS CHEMIN GENERAL", "CELS GENERAL PATH  "),
    L("RENDU DETAIL", "DRAW DETAIL "),
    L("PISTOLET BALAYAGE", "LIGHT GUN SWEEP  "),
    L("CD FICHIERS", "CD FILES   "),
    L("TOUT 3.5 (A = PAGE SUIVANTE)", "ALL 3.5 (A = NEXT PAGE)     "),
    L("PHASE IMAGE", "FRAME PHASE"),
    L("CELS INCLINES", "CELS SHEARED"),
    "CELS ROTATIONS",
    L("CELS ACE ECHELLES", "CELS ACE SCALES"),
    L("CELS ACE ETROITS 2", "NARROW ACE CELS 2"),
    L("CELS FONDUS MELANGES", "CELS FADES BLENDS"),
    L("CELS HORS ECRAN", "CELS OFF SCREEN"),
    L("CELS TRANSP FINS", "CELS FINE TRANSP"),
    L("SCENE BANDES", "SCENE BANDS"),
    L("TOUT 3.6 A (REMESURE SYNC)", "ALL 3.6 A (SYNC REMEASURE)"),
    L("TOUT 3.6 B (NOUVEAUX TESTS)", "ALL 3.6 B (NEW TESTS)")
  };

static const char *heads[G_COUNT] =
  {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    L("          CRC32      TEMPS", "          CRC32      TIME "),
    NULL,
    NULL,
    NULL,
    "       CRC32   N INST  EX1  EX2",
    NULL,
    L("     ET       OU       DERNIER  CH", "     AND      OR       LAST     CH"),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "              MIN    MED    MAX US",
    "              MIN    MED    MAX US",
    L("          TEMPS DE LA BOUCLE", "          TIME OF THE LOOP  "),
    L("          CRC32      TEMPS", "          CRC32      TIME "),
    L("CRC32 DE L'ECRAN APRES CHAQUE CEL", "SCREEN CRC32 AFTER EACH CEL      "),
    NULL,
    "              MIN    MED    MAX US",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  };

u32 wait_buttons(u32 mask)
{
  u32 buttons;

  do
    {
      buttons = 0;
      DoControlPad(1, &buttons, 0);
      WaitVBL(b_vbl_ioreq, 1);
    }
  while((buttons & mask) == 0);
  return buttons;
}

static int page_count(int g)
{
  if(g == G_SWEEP || g == G_PIXC || g == G_SCAN || g == G_SW3 || g == G_PK34 || g == G_RDET || g == G_GUNSW ||
     g == G_SBAND)
    return 2;
  if(g == G_PIX3)
    return 5;
  if(g == G_WID || g == G_MULC)
    return 3;
  if(g == G_REGS)
    return 4;
  return 1;
}

static void page_title(char *buf, int g, int page)
{
  char *p;

  p = put_s(buf, "P2I BENCH 3.6 ");
  p = put_s(p, titles[g]);
  if(page_count(g) > 1)
    {
      p = put_s(p, " ");
      p = put_u(p, (u32)(page + 1), 0);
      p = put_s(p, "/");
      p = put_u(p, (u32)page_count(g), 0);
    }
  (void)p;
}

static int needs_cels(int g)
{
  return (g == G_SPEED || g == G_CELS || g == G_RENDER ||
          g == G_SWEEP || g == G_PIXC || g == G_FORMAT || g == G_GUN ||
          g == G_PIX3 || g == G_SW3 || g == G_END || g == G_SCALE ||
          g == G_CUT || g == G_XY || g == G_WID || g == G_HOLE ||
          g == G_ACEP || g == G_SRC || g == G_PK34 || g == G_GUN2 || g == G_GENP || g == G_RDET || g == G_GUNSW ||
          (g >= G_PHASE && g <= G_TRF));
}

/* set while TOUT 3.3 runs: its pages ask nothing */
static int all33;

/* set while TOUT 3.6 A runs: the cel pages of discs 3.1 to 3.5 again, each draw starting right after a VBL */
static int all36a;

/* runs one page of group g and shows it with the given status line */
static void run_page(int g, int page, const char *status)
{
  char title[LINE_CHARS];
  char head[LINE_CHARS];

  page_title(title, g, page);
  b_line_count = 0;
  /* version 3.6: the new cel pages, and every page of TOUT 3.6 A, start each timed draw right after a VBL */
  b_sync = (all36a || (g >= G_PHASE && g <= G_TRF)) ? 1 : 0;
  sync_reset();
  show_page(title, heads[g], L("MESURE EN COURS...", "MEASURING...      "));

  if(needs_cels(g) && cels_setup() < 0)
    text_line(L("PAS DE MEMOIRE", "OUT OF MEMORY "));
  else
    switch(g)
      {
      case G_PIX3:   group_pix3(page);  break;
      case G_SW3:    group_sweep3(page); break;
      case G_SCN:    group_scene();     break;
      case G_CD3:    group_cd3();       break;
      case G_VID:    group_video();     break;
      case G_VLIM:   group_vram_limit(); break;
      case G_ARM3:   group_arm_crc3();  break;
      case G_ARM4:   group_arm_step();  break;
      case G_CD4:    group_cd4();       break;
      case G_SCALE:  group_scale();     break;
      case G_NA:     group_unaligned(); break;
      case G_ARA:    group_arm_aligned(!all33); break;
      case G_MULC:   group_muls(page);  break;
      case G_HOLE:   group_holes();     break;
      case G_WID:    group_widths(page); break;
      case G_XY:     group_xy();        break;
      case G_CUT:    group_cut();       break;
      case G_ACEP:   group_acepos();    break;
      case G_SRC:    group_source();    break;
      case G_PK34:   group_packs34(page); break;
      case G_VIMG:   group_vimg();      break;
      case G_GUN2:   group_gun2();      break;
      case G_CPUMAP: group_cpumap();    break;
      case G_GENP:   group_genpath();   break;
      case G_RDET:   if(page == 0) formats_detail(); else { scene_detail(); geometry_detail(); } break;
      case G_GUNSW:  group_gunsweep(page); break;
      case G_CDF:    group_cdfiles();   break;
      case G_PHASE:  group_phase();     break;
      case G_SHEAR:  group_shear();     break;
      case G_ROT:    group_rot();       break;
      case G_ACESC:  group_acesc();     break;
      case G_NARROW: group_narrow();    break;
      case G_FADE:   group_fade();      break;
      case G_OFF:    group_off();       break;
      case G_TRF:    group_trf();       break;
      case G_SBAND:  if(page == 0) scene_bands(); else scene_adds(); break;
      case G_END:    group_endurance(); break;
      case G_SWEEP:  group_sweep(page); break;
      case G_PIXC:   group_pixc(page);  break;
      case G_FORMAT: group_formats();   break;
      case G_MATH2:  group_math2();     break;
      case G_MEM:    group_mem();       break;
      case G_ARMT:   group_arm_time();  break;
      case G_ARMC:   group_arm_crc();   break;
      case G_DSP:    group_dsp();       break;
      case G_CD2:    group_cd2();       break;
      case G_REGT:   group_regtime();   break;
      case G_SCAN:   group_scan(page);  break;
      case G_SYS:    group_system();    break;
      case G_PBUS:   group_pbus();      break;
      case G_GUN:    group_gun();       break;
      case G_CLOCK:  group_clock();     break;
      case G_SPEED:  group_speed();     break;
      case G_CELS:   group_cels();      break;
      case G_CPU:    group_cpu();       break;
      case G_MATH:   group_math();      break;
      case G_RENDER: group_render();    break;
      case G_HW:     group_hw();        break;
      case G_CD:     group_cd();        break;
      case G_REGS:   group_regs(page);  break;
      default:       break;
      }

  if(b_sync && heads[g] == NULL)
    {
      sync_head(head);
      show_page(title, head, status);
    }
  else
    show_page(title, heads[g], status);
  b_sync = 0;
}

static void run_group(int g)
{
  u32 buttons;
  int page;
  int pages;
  char title[LINE_CHARS];

  if(g == G_MIRE)
    {
      if(cels_setup() >= 0)
        group_mire();
      return;
    }
  if(g == G_PBL || g == G_SC3)
    {
      page_title(title, g, 0);
      b_line_count = 0;
      show_page(title, NULL, "");
      if(g == G_PBL)
        group_pb_live();
      else
        group_scan3();
      return;
    }
  pages = page_count(g);
  for(page = 0; page < pages; page++)
    {
      run_page(g, page, (page + 1 < pages) ? L("A: PAGE SUIVANTE   X: MENU", "A: NEXT PAGE   X: MENU    ") : L("A OU X: MENU", "A OR X: MENU"));
      buttons = wait_buttons(ControlA | ControlStart | ControlX);
      if(buttons & ControlX)
        break;
    }
}

static void run_all(void)
{
  u32 buttons;
  int g;
  int page;

  for(g = G_SWEEP; g <= G_CLOCK; g++)
    for(page = 0; page < page_count(g); page++)
      {
        run_page(g, page, (g == G_CLOCK) ? L("FIN - A OU X: MENU", "END - A OR X: MENU") : L("A: SUITE   X: MENU", "A: NEXT   X: MENU "));
        buttons = wait_buttons(ControlA | ControlStart | ControlX);
        if(buttons & ControlX)
          return;
      }
}

/* every page of version 3.3 but ARM NON ALIGNE, which can stop the console */
static void run_all33(void)
{
  static const u8 list[6] = { G_CUT, G_XY, G_WID, G_HOLE, G_MULC, G_ARA };
  u32 buttons;
  int i;
  int g;
  int page;

  all33 = 1;
  for(i = 0; i < 6; i++)
    {
      g = list[i];
      for(page = 0; page < page_count(g); page++)
        {
          run_page(g, page, (i == 5) ? L("FIN - A OU X: MENU", "END - A OR X: MENU") : L("A: SUITE   X: MENU", "A: NEXT   X: MENU "));
          buttons = wait_buttons(ControlA | ControlStart | ControlX);
          if(buttons & ControlX)
            {
              all33 = 0;
              return;
            }
        }
    }
  all33 = 0;
}

/* every new page of version 3.4; the last one waits for A, after the photograph */
static void run_all34(void)
{
  static const u8 list[4] = { G_ACEP, G_SRC, G_PK34, G_VIMG };
  u32 buttons;
  int i;
  int g;
  int page;

  for(i = 0; i < 4; i++)
    {
      g = list[i];
      for(page = 0; page < page_count(g); page++)
        {
          run_page(g, page, (i == 3) ? L("FIN - A OU X: MENU", "END - A OR X: MENU") : L("A: SUITE   X: MENU", "A: NEXT   X: MENU "));
          buttons = wait_buttons(ControlA | ControlStart | ControlX);
          if(buttons & ControlX)
            return;
        }
    }
}

/* every new page of version 3.5; the last one waits for A, after the photograph */
static void run_all35(void)
{
  static const u8 list[4] = { G_CPUMAP, G_GENP, G_CDF, G_RDET };
  u32 buttons;
  int i;
  int g;
  int page;

  for(i = 0; i < 4; i++)
    {
      g = list[i];
      for(page = 0; page < page_count(g); page++)
        {
          run_page(g, page, (i == 3 && page + 1 == page_count(g)) ? L("FIN - A OU X: MENU", "END - A OR X: MENU") : L("A: SUITE   X: MENU", "A: NEXT   X: MENU "));
          buttons = wait_buttons(ControlA | ControlStart | ControlX);
          if(buttons & ControlX)
            return;
        }
    }
}

/* runs the pages of a list of groups, one after the other; the last page waits for A after the photograph */
static void run_list(const u8 *list, int n)
{
  u32 buttons;
  int i;
  int g;
  int page;

  for(i = 0; i < n; i++)
    {
      g = list[i];
      for(page = 0; page < page_count(g); page++)
        {
          run_page(g, page, (i == n - 1 && page + 1 == page_count(g)) ? L("FIN - A OU X: MENU", "END - A OR X: MENU") : L("A: SUITE   X: MENU", "A: NEXT   X: MENU "));
          buttons = wait_buttons(ControlA | ControlStart | ControlX);
          if(buttons & ControlX)
            return;
        }
    }
}

/* TOUT 3.6 A: the cel pages compared with the core (discs 3.0 to 3.5) measured again, each draw starting right after
   a VBL (17 pages) */
static void run_all36a(void)
{
  static const u8 list[12] = { G_CUT, G_XY, G_WID, G_HOLE, G_SWEEP, G_SW3, G_SCN, G_ACEP, G_SRC, G_PK34, G_GENP, G_SCALE };

  all36a = 1;
  run_list(list, 12);
  all36a = 0;
}

/* TOUT 3.6 B: the new pages of version 3.6 (10 pages) */
static void run_all36b(void)
{
  static const u8 list[9] = { G_PHASE, G_SHEAR, G_ROT, G_ACESC, G_NARROW, G_FADE, G_OFF, G_TRF, G_SBAND };

  run_list(list, 9);
}

static void draw_menu(int selected)
{
  int i;
  int top;
  GrafCon gc;

  top = (selected >= VISIBLE) ? selected - VISIBLE + 1 : 0;
  clear_screen(0);
  draw_row(0, 0, "P2I LAB 3DO BENCH 3.6", MakeRGB15(31, 31, 0));
  draw_row(0, 1, L("HAUT/BAS, A: LANCER, X: QUITTER", "UP/DOWN, A: START, X: QUIT     "), MakeRGB15(18, 18, 18));
  for(i = 0; i < VISIBLE && top + i < G_COUNT; i++)
    {
      memset(&gc, 0, sizeof(gc));
      SetFGPen(&gc, (top + i == selected) ? MakeRGB15(31, 31, 0) : MakeRGB15(31, 31, 31));
      SetBGPen(&gc, MakeRGB15(0, 0, 0));
      MoveTo(&gc, 32, 14 + (3 + i) * 9);
      DrawText8(&gc, b_sc.sc_BitmapItems[0], (const u8 *)titles[top + i]);
    }
  DisplayScreen(b_sc.sc_Screens[0], 0);
}

static void menu(void)
{
  u32 buttons;
  int selected;

  selected = 0;
  for(;;)
    {
      draw_menu(selected);
      buttons = wait_buttons(ControlUp | ControlDown | ControlA | ControlStart | ControlX);
      if(buttons & ControlX)
        return;
      if(buttons & ControlUp)
        selected = (selected + G_COUNT - 1) % G_COUNT;
      else if(buttons & ControlDown)
        selected = (selected + 1) % G_COUNT;
      else if(selected == G_ALL)
        run_all();
      else if(selected == G_ALL33)
        run_all33();
      else if(selected == G_ALL34)
        run_all34();
      else if(selected == G_ALL35)
        run_all35();
      else if(selected == G_ALL36A)
        run_all36a();
      else if(selected == G_ALL36B)
        run_all36b();
      else
        run_group(selected);
    }
}

int main(int argc, char **argv)
{
  Err err;
  int guns;

  (void)argc;
  (void)argv;

  err = InitControlPad(1);
  if(err < 0)
    {
      PrintfSysErr(err);
      return (int)err;
    }
  err = CreateBasicDisplay(&b_sc, DI_TYPE_DEFAULT, 2);
  if(err < 0)
    {
      PrintfSysErr(err);
      KillControlPad();
      return (int)err;
    }
  b_vbl_ioreq   = CreateVBLIOReq();
  b_vram_ioreq  = CreateVRAMIOReq();
  b_timer_ioreq = GetTimerIOReq();
  OpenMathFolio();
  OpenAudioFolio();
  cels_setup();

  /* The gun pages used to start by themselves when guns were found; they are now chosen from the menu. The two
     conditions below can never be true; they stay (with values instead of being removed) so that the code keeps
     its size and its place in memory, as on the burned disc 3.5: the measurements stay the same. */
  guns = gun_count();
  if(guns >= 100)
    run_group(G_GUN2);
  else if(guns == -100)
    run_group(G_GUN);

  menu();

  hw_close();
  cels_free();
  DeleteIOReq(b_vram_ioreq);
  DeleteIOReq(b_vbl_ioreq);
  DeleteBasicDisplay(&b_sc);
  KillControlPad();
  return 0;
}
