# P2i Lab bench 2.3 measurement disc: real Japanese FZ-10 versus core 1109c

Measurements of 2026-11-09. Times in microseconds.

- **FZ-10:** the tester's real console, CD-R burned at 10x (disc image SHA-1 `BCD5F38ACD453E4D7F0D778EF5FF5182CF451E20`), figures read off the photo.
- **Core 1109c:** simulation (build `int1108b-all5`, BIOS `panafz10.bin`, run `/root/devkit/b23a_panafz10`), not the real MiSTer.
- The system clock advances in steps of 16 µs on the console.
- The MAX column sometimes contains a jump of about ±1,7 s (huge number). It shows on the console, in the simulation and in Opera. Only MIN and MED are used.

## VITESSE (speed)

Photo: `photos/01-vitesse.jpg`.

| Test | FZ-10 MIN | FZ-10 MED | Core MIN | Core MED | Core / FZ-10 (MED) |
|---|---:|---:|---:|---:|---:|
| VRAM CLR | 240 | 240 | 208 | 208 | 0,867 |
| CEL 1PX | 112 | 128 | 96 | 112 | 0,875 |
| CEL FOND (1 stretched pixel, full screen) | 6688 | 6848 | 6640 | 6672 | 0,974 |
| CEL IMAGE (320x240, 6-bit packed) | 10080 | 10864 | 7760 | 7936 | 0,730 |
| STRAHL F+I | 17536 | 17552 | 14528 | 15040 | 0,857 |
| CEL 16BPP | 15680 | 16304 | 12496 | 13104 | 0,804 |
| CEL 8BPP | 11152 | 11920 | 9104 | 9648 | 0,809 |
| CEL DEMI (blend with the screen) | 29008 | 29792 | 18192 | 18224 | 0,612 |
| CEL 8X8 1K (1000 small cels) | 18256 | 18288 | 14016 | 14624 | 0,800 |

| Test (single measurement) | FZ-10 | Core | Core / FZ-10 |
|---|---:|---:|---:|
| VBL X120 | 2001888 (120 VBL) | 2005744 (120 VBL) | 1,002 |
| BOUCLE X60 (background + image, display, VBL wait) | 2001904, **120 VBL** | 1002832, **60 VBL** | |
| CPU REG 1M | 656752 | 607376 | 0,925 |
| CPU MEM 1M | 1441424 | 1214960 | 0,843 |
| MEMCPY 1MB | 135744 | 117584 | 0,866 |
| CD 512K 1 | 1964816 | 784112 | see note |
| CD 512K 2 | 1806336 (ERR 0) | 773968 (ERR 0) | see note |

Notes:
- BOUCLE reproduces the Strahl symptom: 2 frames per iteration on the console, 1 in the core.
- CD: the real MiSTer gives the same times as the simulation (Real MiSTer section, below). The gap with the console is therefore real on these two lines.

## CELS DETAIL (cel detail)

Photo: `photos/02-cels-detail.jpg`. Core: run `b23a_panafz10`, frame 2670.

| Test | FZ-10 MIN | FZ-10 MED | Core MIN | Core MED | Core / FZ-10 (MED) |
|---|---:|---:|---:|---:|---:|
| 16B VRAM (source in VRAM) | 15680 | 16256 | 12960 | 13568 | 0,835 |
| 6BPP NONPK (6-bit unpacked) | 10048 | 10768 | 8032 | 8688 | 0,807 |
| 4BPP | 8960 | 9568 | 7200 | 7344 | 0,768 |
| 1BPP | 7232 | 7408 | 4048 | 4192 | 0,566 |
| PACK REPET (packed, repeats) | 6960 | 7120 | 3728 | 3792 | 0,533 |
| PACK TRANS (packed, all transparent) | 4288 | 4320 | 1792 | 1808 | 0,419 |
| 16B 1/2 (scaled down by half) | 10224 | 10944 | 6848 | 6960 | 0,636 |
| 16B X2 (160x120 scaled up two times) | 9008 | 9600 | 7136 | 7232 | 0,753 |
| 16B ROT45 | 15296 | 15856 | 10144 | 10768 | 0,679 |
| 16B HORS (entirely off screen) | 8224 | 8832 | 304 | 304 | 0,034 |
| 8BPP TRANS (every other colour transparent) | 9168 | 9792 | 5728 | 5856 | 0,598 |
| FOND DEMI | 22000 | 22160 | 13168 | 13776 | 0,622 |
| IMAGE DEMI | 24736 | 25456 | 11280 | 11888 | 0,467 |
| 16B 8X240 | 592 | 608 | 544 | 576 | 0,947 |
| 16B 320X8 | 624 | 640 | 496 | 512 | 0,800 |
| 32X32 X100 | 22864 | 22896 | 19232 | 19280 | 0,842 |

First leads, to be confirmed:
- 1:1 drawing: PACK REPET gives about 93 ns per pixel on the console and 49 ns in the core, while the stretched fill (CEL FOND) is almost right (89 ns and 87 ns).
- An off-screen cel is not free on the console (8,8 ms); the core skips it (0,3 ms).
- Walking over transparent pixels costs 56 ns per pixel on the console, 24 ns in the core.
- Blending with the screen adds 13 to 15 ms on the console, 4 to 7 ms in the core.

## CPU DETAIL

Photo: `photos/03-cpu-detail.jpg`. Core: run `b23a_panafz10`, frame 3870.

| Test (total loop time) | FZ-10 | Core | Core / FZ-10 |
|---|---:|---:|---:|
| MUL X3 1M | 1993728 | 1882272 | 0,944 |
| MUL XG 1M | 1992960 | 1883072 | 0,945 |
| LDR DRAM | 1093360 | 954304 | 0,873 |
| STR DRAM | 1005328 | 866304 | 0,862 |
| LDR VRAM | 1093264 | 955216 | 0,874 |
| STR VRAM | 1004688 | 866544 | 0,863 |
| CPY D>V 1M | 135760 | 117744 | 0,867 |
| CPY V>D 1M | 135904 | 117648 | 0,866 |
| LDR ALEA | 240224 | 204592 | 0,852 |
| APPELS 1M | 1445168 | 1302672 | 0,901 |
| DIV 100K | 1985488 | 1946128 | 0,980 |
| SAUTS 1M | 2622368 | 2520224 | 0,961 |
| SWI 10K | 230144 | 193712 | 0,842 |

First leads, to be confirmed:
- The LDR and STR loops run 999 424 iterations. The console - core gap is about 139 ns per iteration, the same for reads and for writes, in DRAM as in VRAM.
- The CPU runs at the same speed in DRAM and in VRAM on the console, as in the core.
- MUL X3 and MUL XG take the same time on both: this test says nothing about the variable duration of multiplications (the operand chosen by the compiler is not known).

## MATH MADAM

Photo: `photos/04-math-madam.jpg`. Core: run `b23b_panafz10`, frame 1680. Opera: BIOS FZ-10, bench 2.3, frame 6480.

| Function | FZ-10 CRC | Core CRC | Opera CRC | Core = FZ-10 | Opera = FZ-10 |
|---|---|---|---|---|---|
| V3XM33 | AC2A52A6 | 49697CC1 | AC2A52A6 | no | yes |
| M33XM33 | 07F7A98C | 4503534B | 07F7A98C | no | yes |
| V4XM44 | B90CABAB | 7806AC1A | B90CABAB | no | yes |
| M44XM44 | 77ADF19D | 970E1762 | 77ADF19D | no | yes |
| MANY V3 | 76275A19 | 85A227D6 | 76275A19 | no | yes |
| DIVZ MANY | CC685F2A | 010C338E | D8BA97E6 | no | no |
| DIVZ 1 | CC685F2A | 010C338E | D8BA97E6 | no | no |
| DOT3 | 3561C40C | F9FD50B7 | 3561C40C | no | yes |
| CROSS3 | E7142E1B | EE73F4DA | E7142E1B | no | yes |
| MANY F16 | 56CDB9C9 | 56CDB9C9 | 56CDB9C9 | yes | yes |
| SCALAIRE | B4757164 | B4757164 | B4757164 | yes | yes |
| ABS V3 | F03D960E | EABA72EE | F03D960E | no | yes |
| LOGICIEL | 47FC3013 | 47FC3013 | 47FC3013 | yes | yes |

| Function (total time) | FZ-10 | Core | Core / FZ-10 |
|---|---:|---:|---:|
| V3XM33 | 12928 | 11168 | 0,864 |
| M33XM33 | 7152 | 6848 | 0,957 |
| V4XM44 | 14288 | 12064 | 0,844 |
| M44XM44 | 9392 | 7856 | 0,836 |
| MANY V3 | 1200 | 1024 | 0,853 |
| DIVZ MANY | 2464 | 1488 | 0,604 |
| DIVZ 1 | 13952 | 11552 | 0,828 |
| DOT3 | 1338304 (clock jump, unusable) | 10288 | |
| CROSS3 | 13024 | 11168 | 0,857 |
| MANY F16 | 1904 | 1696 | 0,891 |
| SCALAIRE | 1840 | 1632 | 0,887 |
| ABS V3 | 15056 | 13216 | 0,878 |
| LOGICIEL | 10064 | 9312 | 0,925 |

Notes:
- A different CRC says that at least one result differs, not by how much.
- The console DIVZ CRC is read off the photo, identical on its two lines; the third character (6) is the least sharp.
- The core CRCs come from the simulation. **On the real MiSTer, only the two DIVZ lines differ from the console**: see the "Real MiSTer" section.

## RENDU CRC (draw CRC)

Photo: `photos/05-rendu-crc.jpg`. Core: run `b23b_panafz10`, frame 2550. Opera: BIOS FZ-10, bench 2.3, frame 7680.

CRC32 of the whole screen after a 16-bit background, then the listed cel.

| No. | Cel | FZ-10 CRC | Core CRC | Opera CRC | Core = FZ-10 | Opera = FZ-10 |
|---|---|---|---|---|---|---|
| R01 | background only | 8EC34E0F | 8EC34E0F | 8EC34E0F | yes | yes |
| R02 | Strahl image (6-bit packed) | 6DCBE1C2 | 6DCBE1C2 | 6DCBE1C2 | yes | yes |
| R03 | 8-bit coded | BDB495A7 | BDB495A7 | BDB495A7 | yes | yes |
| R04 | Strahl background (1 stretched pixel) | 1DC02C97 | C42012A2 | 1DC02C97 | **no** | yes |
| R05 | 16-bit blended | 8EC34E0F | 8EC34E0F | 8EC34E0F | yes | yes |
| R06 | 6-bit unpacked | 8AD0EA09 | 8AD0EA09 | 8AD0EA09 | yes | yes |
| R07 | 4-bit | 0D02C350 | 0D02C350 | 0D02C350 | yes | yes |
| R08 | 1-bit | A341EF4B | A341EF4B | A341EF4B | yes | yes |
| R09 | packed, repeats | 6ACFB2AA | 6ACFB2AA | 6ACFB2AA | yes | yes |
| R10 | packed, transparent | 8EC34E0F | 8EC34E0F | 8EC34E0F | yes | yes |
| R11 | scaled down by half | 09ECE263 | 09ECE263 | 09ECE263 | yes | yes |
| R12 | scaled up two times | 8F17CB11 | 8F17CB11 | 8F17CB11 | yes | yes |
| R13 | rotation 45° | D9671996 | D9671996 | E048DF6D | yes | **no** |
| R14 | 8-bit, transparency | 628AC09F | 628AC09F | 628AC09F | yes | yes |
| R15 | 100 cels of 32x32 | 1CA61332 | 1CA61332 | 1CA61332 | yes | yes |
| R16 | Strahl background, blended | 9D4C93A5 | 9D4C93A5 | 9D4C93A5 | yes | yes |
| R17 | Strahl image, blended | C7F6FE62 | C7F6FE62 | C7F6FE62 | yes | yes |
| R18 | 1000 cels of 8x8 | 79A6457D | 79A6457D | 79A6457D | yes | yes |
| R19 | rotation, scale, deformation (HDDX/HDDY) | 90E2C343 | 90E2C343 | 01228BB6 | yes | **no** |
| R20 | mirror | 20AD3EEE | 20AD3EEE | 20AD3EEE | yes | yes |
| R21 | unusual PIXC setting A | 8BE5D780 | AB3FB1D7 | 8BE5D780 | **no** | yes |
| R22 | unusual PIXC setting B (USEAV) | 4127CAE5 | 0A640A26 | 7EB38A0E | **no** | **no** |
| R23 | P bit selects the PIXC half | 6BA7FBF8 | 6BA7FBF8 | 6BA7FBF8 | yes | yes |
| R24 | fractional position, clipped | C33318F3 | C33318F3 | E62A00B4 | yes | **no** |

Notes:
- Core: 21 of 24 identical to the console. Opera: 20 of 24.
- The last character of the console's R22 (5) is the least sharp; it differs from the two others anyway.
- R04 lead: the Strahl background is the only opaque cel of the test with PRE1 TLLSB = 0, and the field `TLLSBIPN` is not used anywhere in `rtl/3DO`. Hypothesis to be checked.

## MATERIEL (hardware)

Photo: `photos/06-materiel.jpg`. Core: run `b23c_panafz10`, frame 2280.

| Measurement | FZ-10 | Core |
|---|---|---|
| Free memory DRAM / VRAM | 1343248 / 724992 | 1343248 / 724992 |
| 300 VBL | 5004880 µs (59,94 Hz) | 5014480 µs (59,83 Hz) |
| Audio frames during these 300 VBL | 220033 (≈ 43 964 Hz) | 221138 (≈ 44 100 Hz) |
| Audio ticks | 1196 | 1202 |
| MADAM revision | 01022000 | 01020000 |
| CLIO revision | 02022000 | 02020000 |
| MSYS | 00000029 | 00000029 |
| MCTL | 0001E000 | 00016000 |
| VINT0 / VINT1 (read back) | 00000000 / 00000000 | 00000000 / 00000005 |
| HDELAY | 000000BA | 000000BA |
| CSTAT | 00000000 | 00000000 |
| Observed VCNT min-max | 4-265 | 0-262 |
| Field bit (0 / 1, over 400 reads) | 199 / 201 | 204 / 196 |
| Observed HCNT min-max | 1-1572 | 16-1559 |
| Line at wake-up after WaitVBL | 14 | 13 |
| Register read via svc_mem | 582 µs | 517 µs |
| ROM1 CRC32 / first word | 58242CEE / E1A01001 | 58242CEE / E1A01001 |
| ROM2 CRC32 / first word | 45F478B1 / 015A5A5A | A738EA1C / 00000000 |

Notes:
- Console ROM1 = `panafz10.bin` (identical CRC32). Console ROM2 = `kanji.rom` padded to 1 MB with FF (CRC32 45F478B1 recomputed on the file). The simulation runs without a kanji ROM (1 MB of zeros).
- VCNT and HCNT come from 400 reads spaced about 550 µs apart: these are observed values, not necessarily the true bounds.
- The audio and VBL frequencies rest only on this 5 s measurement.
- The VBL rate of the simulation depends on its clock model: to be confirmed on the real MiSTer.

## CD DETAIL

Photo: `photos/07-cd-detail.jpg`. Core: run `b23d_panafz10`, frame 4380.

| Information | FZ-10 | Core |
|---|---|---|
| Blocks reported | 71745 | 71746 |
| End of disc (TOC) | 15:56:45 | 15:56:46 |
| Status word | 000000E3 | 000000E1 |
| Tracks | 1-1 | 1-1 |
| Session | 0, 0:0:0 | 0, 0:0:0 |
| Track 1 control | 14 | 14 |
| Filler block read back (middle of the disc) | 35872 : P2PD, index 000085E7 | 35873 : P2PD, index 000085E8 |

| Read (512 KB, 16 blocks at a time) | FZ-10 | Per block | Core |
|---|---:|---:|---:|
| SEQ 1X | 3424096 | 13375 | 774000 |
| SEQ 2X | 1720592 | 6721 | 770992 |

| Time of a single-block read | FZ-10 MIN | FZ-10 MED | Core MIN | Core MED |
|---|---:|---:|---:|---:|
| 1 BLOC 2X (consecutive blocks) | 6224 | 6688 | 2880 | 2976 |
| MEME BLOC | 67328 | 67872 | 2880 | 2976 |
| SAUT 16 | 93504 | 107552 | 2928 | 2976 |
| SAUT 256 | 91328 | 109424 | 2864 | 2976 |
| SAUT 4096 | 158736 | 178640 | 2912 | 2976 |
| SAUT 4K 1X | 183344 | 220496 | 2880 | 2976 |
| SAUT 32K | 223744 | 278960 | 2912 | 2976 |
| SAUT MAX | 280336 | 327248 | 2880 | 2976 |

Notes:
- The simulated drive answers in about 3 ms whatever the distance and the speed mode: this simulation models neither seeks nor speed. Measurement on the real MiSTer: see the "Real MiSTer" section.
- FZ-10: 74,8 blocks/s at 1X and 148,8 blocks/s at 2X.
- Reading the same block again costs 68 ms on the console, while a consecutive block costs 6,7 ms.
- The console status word E3 has the double-speed bit (02), absent from the core at the time of the read.
- The reported blocks and the end of disc differ by one unit between the console and the core.

## REGISTRES 1/4 (registers, MADAM 000-07C)

Photo: `photos/08-registres-1.jpg`. Core: run `b23c_panafz10`, frame 2730.

| Address | FZ-10 | Core |
|---|---|---|
| 000 | 01022000 | 01020000 |
| 004 | 00000029 | 00000029 |
| 008 | 0001E000 | 00016000 |
| 00C | 00178906 | 00000000 |
| 020 | 00000000 | 00000000 |
| 024 | 00000000 | 00000000 |
| 028 | 00000000 | 00000000 |
| 040 | 000002BF | 00000000 |
| all others | E2800001 | 00000000 |

Note: E2800001 is the ARM machine code of `ADD R0,R0,#1`. These addresses do not seem to match any register: the read would return what is left on the bus (interpretation, not checked).

## REGISTRES 2/4 (registers, MADAM 080-0FC)

Photo: `photos/09-registres-2.jpg`. Core: run `b23c_panafz10`, frame 2940.

All 32 addresses read E2800001 on the console and 00000000 in the core.

## REGISTRES 3/4 (registers, CLIO 000-07C)

Photo: `photos/10-registres-3.jpg`. Core: run `b23c_panafz10`, frame 3150.

| Address | FZ-10 | Core | Remark |
|---|---|---|---|
| 000 | 02022000 | 02020000 | revision |
| 00C | 00000000 | 00000005 | VINT1 read back |
| 024 | C0050F0F | 00000000 | |
| 030 | 000004AE | 000001AA | HCNT, changes on every read |
| 034 | 0000087F | 0000002C | VCNT, changes on every read |
| 038 | C693B70F | C693B70F | identical |
| 03C | E357E5B7 | 00000003 | single read |
| 040, 044 | 00000088 | 00000081 | pending interrupts, vary |
| 048, 04C | E0000A06 | E0000A06 | identical |
| 050, 054 | 1FFF0000 | 1FFF0000 | identical |
| 060, 064 | 00000002 | 00000000 | pending interrupts, vary |
| 068, 06C | 00000101 | 00000101 | identical |
| all others | 00000000 | 00000000 | |

Note: CLIO returns 0 at empty addresses, while MADAM returns the value left over on the bus.

## REGISTRES 4/4 (registers, CLIO 080-0BC and timers 100-13C)

Photo: `photos/11-registres-4.jpg`. Core: run `b23c_panafz10`, frame 3390.

| Address | FZ-10 | Core | Remark |
|---|---|---|---|
| 080 | 000000BA | 000000BA | HDELAY, identical |
| 084 | 00000076 | 00000000 | |
| 088 | 00000001 | 00000000 | |
| 100 | 0000BE3E | 0000BC9C | counter, varies |
| 104 | 0000F423 | 0000F423 | reload, identical |
| 108 | 0000F7B1 | 0000FFCF | counter, varies |
| 10C | 0000FFFF | 0000FFFF | identical |
| 110, 114 | 0000FFFF | 0000FFFF | identical |
| 118 | 0000014B | 000002B5 | counter, varies |
| 11C | 000003A8 | 000003A8 | reload, identical |
| 120, 124 | 0000FFFD, 0000FFFF | 0000FFFD, 0000FFFF | identical |
| 130 | 0000F40A | 0000F3EB | counter, varies |
| 134 | 0000FFFF | 0000FFFF | identical |
| 138 | 00000001 | 00000001 | identical |
| all others | 00000000 | 00000000 | |

## Summary (11 pages recorded)

- The CD-R boots on the Japanese FZ-10 and all pages run to the end.
- BOUCLE reproduces Strahl: 120 VBL on the console, 60 in the simulated core.
- Drawing: 21 CRCs out of 24 identical; cel times too short, from −3 % (stretched background) to −97 % (off-screen cel).
- CPU and memory: from −2 % to −16 %.
- Matrix math: 10 CRCs out of 13 different in simulation, but only the 2 DIVZ lines on the real MiSTer.
- CD: real times known. The real MiSTer, like the simulation, models neither the speed nor the head movement: 512 KB in 0,8 s at 1X as at 2X, versus 3,4 s and 1,7 s on the console.
- All "core" figures above come from the simulation; the real MiSTer is recorded in the next section.

## Real MiSTer (core 3DO-test1109c, Main 1107c, BIOS FZ-10)

### VITESSE (speed)

Photo: `photos/mister-01-vitesse.jpg`.

| Test (MED, or single measurement) | FZ-10 | Real MiSTer | Simulation |
|---|---:|---:|---:|
| VBL X120 | 2001888 | 2005728 | 2005744 |
| CPU REG 1M | 656752 | 607584 | 607376 |
| CPU MEM 1M | 1441424 | 1214832 | 1214960 |
| MEMCPY 1MB | 135744 | 117632 | 117584 |
| VRAM CLR | 240 | 208 | 208 |
| CEL 1PX | 128 | 112 | 112 |
| CEL FOND | 6848 | 6672 | 6672 |
| CEL IMAGE | 10864 | 7904 | 7936 |
| STRAHL F+I | 17552 | 15040 | 15040 |
| BOUCLE X60 | 2001904, 120 VBL | 1002848, 60 VBL | 1002832, 60 VBL |
| CEL 16BPP | 16304 | 13104 | 13104 |
| CEL 8BPP | 11920 | 9664 | 9648 |
| CEL DEMI | 29792 | 18224 | 18224 |
| CEL 8X8 1K | 18288 | 14624 | 14624 |
| CD 512K 1 | 1964816 | 786720 | 784112 |
| CD 512K 2 | 1806336 (ERR 0) | 773952 (ERR 0) | 773968 (ERR 0) |

The real MiSTer and the simulation agree within 0,5 % on all these lines.

### CELS DETAIL (cel detail)

Photo: `photos/mister-02-cels-detail.jpg`.

| Test (MED) | FZ-10 | Real MiSTer | Simulation |
|---|---:|---:|---:|
| 16B VRAM | 16256 | 13536 | 13568 |
| 6BPP NONPK | 10768 | 8592 | 8688 |
| 4BPP | 9568 | 7344 | 7344 |
| 1BPP | 7408 | 4128 | 4192 |
| PACK REPET | 7120 | 3808 | 3792 |
| PACK TRANS | 4320 | 1808 | 1808 |
| 16B 1/2 | 10944 | 6960 | 6960 |
| 16B X2 | 9600 | 7264 | 7232 |
| 16B ROT45 | 15856 | 10784 | 10768 |
| 16B HORS | 8832 | 304 | 304 |
| 8BPP TRANS | 9792 | 5856 | 5856 |
| FOND DEMI | 22160 | 13776 | 13776 |
| IMAGE DEMI | 25456 | 11888 | 11888 |
| 16B 8X240 | 608 | 576 | 576 |
| 16B 320X8 | 640 | 512 | 512 |
| 32X32 X100 | 22896 | 19280 | 19280 |

The real MiSTer and the simulation agree within 1,6 % on all these lines; the largest gap is 1BPP (4128 versus 4192), and 10 lines out of 16 are identical. MiSTer column re-read on the smoothed photo: the 48 values of the page (MIN, MED, MAX) are multiples of 16. The MAX of 8BPP TRANS (1544000) is a timer jump, as on the console.

### CPU DETAIL

Photo: `photos/mister-03-cpu-detail.jpg`.

| Test (total loop time) | FZ-10 | Real MiSTer | Simulation | MiSTer / FZ-10 |
|---|---:|---:|---:|---:|
| MUL X3 1M | 1993728 | 1883248 | 1882272 | 0,945 |
| MUL XG 1M | 1992960 | 1883168 | 1883072 | 0,945 |
| LDR DRAM | 1093360 | 954272 | 954304 | 0,873 |
| STR DRAM | 1005328 | 866512 | 866304 | 0,862 |
| LDR VRAM | 1093264 | 954144 | 955216 | 0,873 |
| STR VRAM | 1004688 | 867360 | 866544 | 0,863 |
| CPY D>V 1M | 135760 | 117664 | 117744 | 0,867 |
| CPY V>D 1M | 135904 | 117808 | 117648 | 0,867 |
| LDR ALEA | 240224 | 204592 | 204592 | 0,852 |
| APPELS 1M | 1445168 | 1301600 | 1302672 | 0,901 |
| DIV 100K | 1985488 | 1947120 | 1946128 | 0,981 |
| SAUTS 1M | 2622368 | 2520304 | 2520224 | 0,961 |
| SWI 10K | 230144 | 193680 | 193712 | 0,842 |

The real MiSTer and the simulation agree within 0,14 % on all these lines. MiSTer column re-read on the smoothed photo: the 13 values are multiples of 16.

### MATH MADAM

Photo: `photos/mister-04-math-madam.jpg`.

| Function | FZ-10 CRC | Real MiSTer CRC | Simulation CRC | MiSTer = FZ-10 | MiSTer = simulation |
|---|---|---|---|---|---|
| V3XM33 | AC2A52A6 | AC2A52A6 | 49697CC1 | yes | no |
| M33XM33 | 07F7A98C | 07F7A98C | 4503534B | yes | no |
| V4XM44 | B90CABAB | B90CABAB | 7806AC1A | yes | no |
| M44XM44 | 77ADF19D | 77ADF19D | 970E1762 | yes | no |
| MANY V3 | 76275A19 | 76275A19 | 85A227D6 | yes | no |
| DIVZ MANY | CC685F2A | 3A96D7B5 | 010C338E | **no** | no |
| DIVZ 1 | CC685F2A | 3A96D7B5 | 010C338E | **no** | no |
| DOT3 | 3561C40C | 3561C40C | F9FD50B7 | yes | no |
| CROSS3 | E7142E1B | E7142E1B | EE73F4DA | yes | no |
| MANY F16 | 56CDB9C9 | 56CDB9C9 | 56CDB9C9 | yes | yes |
| SCALAIRE | B4757164 | B4757164 | B4757164 | yes | yes |
| ABS V3 | F03D960E | F03D960E | EABA72EE | yes | no |
| LOGICIEL | 47FC3013 | 47FC3013 | 47FC3013 | yes | yes |

| Function (total time) | FZ-10 | Real MiSTer | Simulation |
|---|---:|---:|---:|
| V3XM33 | 12928 | 11168 | 11168 |
| M33XM33 | 7152 | 6864 | 6848 |
| V4XM44 | 14288 | 12080 | 12064 |
| M44XM44 | 9392 | 7856 | 7856 |
| MANY V3 | 1200 | 1008 | 1024 |
| DIVZ MANY | 2464 | 1472 | 1488 |
| DIVZ 1 | 13952 | 11536 | 11552 |
| DOT3 | clock jump | 10288 | 10288 |
| CROSS3 | 13024 | 11168 | 11168 |
| MANY F16 | 1904 | 1680 | 1696 |
| SCALAIRE | 1840 | 1648 | 1632 |
| ABS V3 | 15056 | 13216 | 13216 |
| LOGICIEL | 10064 | 9296 | 9312 |

Notes:
- 11 CRCs out of 13 are identical to the console. The 8 matrix lines that are wrong in simulation are right on the FPGA: the gap came from the simulation, not from the core.
- Only the two DIVZ lines differ from the console (3A96D7B5 versus CC685F2A). This is the only real computation gap of the core on this page.
- Cause of the simulation error, confirmed: at `rtl/3DO/MADAM.sv:339`, the unsigned `'0` makes the whole expression unsigned, and Verilator 5.020 then pads the operands of the product with zeros. Isolated test: -1,0 x 2,5 gives 7FFD8000 instead of FFFD8000 on bits [47:16]. Quartus evidently produces the signed product, since the CRCs are right. Confirmation: with `64'sd0` in place of `'0`, the only build difference (checked), the simulation gives back the 13 CRCs of the real MiSTer, DIVZ included, with times identical to within 16 µs (run `b23m_signed`, frame 1740, screenshot `photos/sim-math-zero-signe.png`). This fix exists only in a working copy.

#### DIVZ: the console rule, found offline

Method: a model on a PC, with the inputs of the disc (`src/bench/math.c`), reproduces exactly the CRC of the core (3A96D7B5) and that of Opera (D8BA97E6). Out of 327 888 rules tried, only those of the following form give the CRC of the console (CC685F2A):
- 64-bit quotient N / Z;
- quotient brought to 16.16 **by truncating toward zero**;
- multiplication by x and y in 16.16, result on bits [47:16], like the other computations.

The core brings the quotient to 16.16 by taking its bits [47:16] (`rtl/3DO/MADAM.sv:316`), which rounds toward minus infinity: for a negative Z, the factor is one unit too large in absolute value.

Effect on the 256 vectors of the test: only the 128 vectors with negative Z differ (256 x and y values out of 512), by at most 584 units of the 16-bit fraction, i.e. 0,0089.

Not determined by this test: the case of a negative N (here n = 256 everywhere), the rounding of the low 16 bits of the quotient and bit 0 of Z, with no effect on these inputs.

Test of the fix in simulation: with the signed zero and, on the quotient line, `MATH_M <= MATH_DIV_RES[47:16] + ((MATH_DIV_RES[63] && MATH_DIV_RES[15:0] != 16'h0000) ? 32'd1 : 32'd0);`, **the 13 CRCs of the page are identical to those of the FZ-10** (DIVZ: CC685F2A on both lines; run `b23m_divz`, frame 1740, screenshot `photos/sim-math-divz.png`).

Disc 3.0 confirms the rule on the FZ-10: truncating toward zero gives back exactly the 6 other CRCs of the MATH EXTREMES page (negative N, fractional N, very small N, very large N, negative fractional N, very small Z), and Z = 0 amounts to a factor of -1 (`v3/p06-math-extremes.txt`).

This fix exists only in a working copy and has not been tried on the real MiSTer. The path to `MATH_M` is declared as a "false path" (`3DO.sdc:19`): the Quartus timing analysis will not check this addition.

### RENDU CRC (draw CRC)

Photo: `photos/mister-05-rendu-crc.jpg`.

The 24 CRCs of the real MiSTer are identical to those of the simulation (automatic comparison with the table above). 21 out of 24 therefore match the console, and the three gaps are the same:

| No. | Cel | FZ-10 CRC | Real MiSTer CRC |
|---|---|---|---|
| R04 | Strahl background (1 stretched pixel) | 1DC02C97 | C42012A2 |
| R21 | unusual PIXC setting A | 8BE5D780 | AB3FB1D7 |
| R22 | unusual PIXC setting B (USEAV) | 4127CAE5 | 0A640A26 |

### MATERIEL (hardware)

Photo: `photos/mister-06-materiel.jpg`.

| Measurement | FZ-10 | Real MiSTer | Simulation |
|---|---|---|---|
| Free memory DRAM / VRAM | 1343248 / 724992 | **1310480** / 724992 | 1343248 / 724992 |
| 300 VBL | 5004880 µs (59,94 Hz) | 5014464 µs (59,83 Hz) | 5014480 µs (59,83 Hz) |
| Audio frames during these 300 VBL | 220033 (≈ 43 964 Hz) | 221136 (≈ 44 100 Hz) | 221138 (≈ 44 100 Hz) |
| Audio ticks | 1196 | 1201 | 1202 |
| MADAM revision | 01022000 | 01020000 | 01020000 |
| CLIO revision | 02022000 | 02020000 | 02020000 |
| MSYS | 00000029 | 00000029 | 00000029 |
| MCTL | 0001E000 | 0001E000 | 00016000 |
| VINT0 / VINT1 (read back) | 00000000 / 00000000 | 00000000 / 00000005 | 00000000 / 00000005 |
| HDELAY | 000000BA | 000000BA | 000000BA |
| CSTAT | 00000000 | 00000000 | 00000000 |
| Observed VCNT min-max | 4-265 | 0-262 | 0-262 |
| Field bit (0 / 1, over 400 reads) | 199 / 201 | 205 / 195 | 204 / 196 |
| Observed HCNT min-max | 1-1572 | 16-1558 | 16-1559 |
| Line at wake-up after WaitVBL | 14 | 13 | 13 |
| Register read via svc_mem | 582 µs | 514 µs | 517 µs |
| ROM1 CRC32 / first word | 58242CEE / E1A01001 | 58242CEE / E1A01001 | 58242CEE / E1A01001 |
| ROM2 CRC32 / first word | 45F478B1 / 015A5A5A | 45F478B1 / 015A5A5A | A738EA1C / 00000000 |

Notes:
- The MiSTer has 32768 bytes less free DRAM than the console and the simulation. Cause unknown: a single measurement.
- MCTL: bit 15 is cleared by `PLAYER_INT` (`rtl/3DO/MADAM.sv:242`). Its value depends on the moment of the read: this same MiSTer reads 0001E000 on this page and 00016000 on the REGISTRES 1/4 page.
- The VBL rate (59,83 Hz) and the audio (44 100 Hz) of the simulation are confirmed on the FPGA; the console gives 59,94 Hz and about 43 964 Hz.
- ROM2 is identical to the console because the SD card holds the `kanji.rom` padded with FF.

### CD DETAIL

Photo: `photos/mister-07-cd-detail.jpg`.

| Information | FZ-10 | Real MiSTer | Simulation |
|---|---|---|---|
| Blocks reported | 71745 | 71746 | 71746 |
| End of disc (TOC) | 15:56:45 | 15:56:46 | 15:56:46 |
| Status word | 000000E3 | 000000E1 | 000000E1 |
| Tracks | 1-1 | 1-1 | 1-1 |
| Session | 0, 0:0:0 | 0, 0:0:0 | 0, 0:0:0 |
| Track 1 control | 14 | 14 | 14 |
| Filler block read back (middle of the disc) | 35872 : P2PD, index 000085E7 | 35873 : P2PD, index 000085E8 | 35873 : P2PD, index 000085E8 |

| Read (512 KB, 16 blocks at a time) | FZ-10 | Real MiSTer | Per block (MiSTer) | Simulation | FZ-10 / MiSTer |
|---|---:|---:|---:|---:|---:|
| SEQ 1X | 3424096 | 817968 | 3195 | 774000 | 4,19 |
| SEQ 2X | 1720592 | 815936 | 3187 | 770992 | 2,11 |

| Time of a single-block read | FZ-10 MED | MiSTer MIN | MiSTer MED | MiSTer MAX | Simulation MED |
|---|---:|---:|---:|---:|---:|
| 1 BLOC 2X (consecutive blocks) | 6688 | 4432 | 5968 | 7504 | 2976 |
| MEME BLOC | 67872 | 5520 | 5968 | 6400 | 2976 |
| SAUT 16 | 107552 | 5232 | 5952 | 6048 | 2976 |
| SAUT 256 | 109424 | 5712 | 5968 | 10416 | 2976 |
| SAUT 4096 | 178640 | 5024 | 5968 | 6960 | 2976 |
| SAUT 4K 1X | 220496 | 5824 | 5968 | 6112 | 2976 |
| SAUT 32K | 278960 | 4992 | 5968 | 6976 | 2976 |
| SAUT MAX | 327248 | 5280 | 5968 | 7168 | 2976 |

Notes:
- The real MiSTer models neither the speed (same time at 1X and at 2X) nor the head movement (6 ms whatever the distance, versus 108 to 327 ms on the console).
- In continuous reading, it reads a block in 3,2 ms, versus 13,4 ms at 1X and 6,7 ms at 2X on the console.
- It costs about 3 ms more per request than the simulation: 3,0 ms for a single block (5968 - 2976), 2,7 to 2,8 ms for 16 blocks. The simulation probably does not count the response time of Main and of the SD card (not checked).
- The disc reports one unit less on the console (burned CD-R) than on the MiSTer (disc image): 71745 versus 71746. Cause not established.
- The 20 times of the page are multiples of 16. The value 50325044 on the PAD line is indeed "P2PD" in ASCII.

### REGISTRES 1/4 (registers, MADAM 000-07C)

Photo: `photos/mister-08-registres-1.jpg`.

| Address | FZ-10 | Real MiSTer | Simulation |
|---|---|---|---|
| 000 | 01022000 | 01020000 | 01020000 |
| 004 | 00000029 | 00000029 | 00000029 |
| 008 | 0001E000 | 00016000 | 00016000 |
| 00C | 00178906 | 00000000 | 00000000 |
| 020, 024, 028 | 00000000 | 00000000 | 00000000 |
| 040 | 000002BF | 00000000 | 00000000 |
| all others | E2800001 | 00000000 | 00000000 |

### REGISTRES 2/4 (registers, MADAM 080-0FC)

Photo: `photos/mister-09-registres-2.jpg`.

All 32 addresses read 00000000, as in simulation (console: E2800001).

### REGISTRES 3/4 (registers, CLIO 000-07C)

Photo: `photos/mister-10-registres-3.jpg`.

| Address | FZ-10 | Real MiSTer | Simulation | Remark |
|---|---|---|---|---|
| 000 | 02022000 | 02020000 | 02020000 | revision |
| 00C | 00000000 | 00000005 | 00000005 | VINT1 read back |
| 024 | C0050F0F | 00000000 | 00000000 | |
| 030 | 000004AE | 000002AC | 000001AA | HCNT, changes on every read |
| 034 | 0000087F | 0000082B | 0000002C | VCNT, changes on every read |
| 038 | C693B70F | C693B70F | C693B70F | identical |
| 03C | E357E5B7 | 00000003 | 00000003 | single read |
| 040, 044 | 00000088 | 00000089 | 00000081 | pending interrupts, vary |
| 048, 04C | E0000A06 | E0000A06 | E0000A06 | identical |
| 050, 054 | 1FFF0000 | 1FFF0000 | 1FFF0000 | identical |
| 060, 064 | 00000002 | 00000000 | 00000000 | pending interrupts, vary |
| 068, 06C | 00000101 | 00000101 | 00000101 | identical |
| all others | 00000000 | 00000000 | 00000000 | |

### REGISTRES 4/4 (registers, CLIO 080-0BC and timers 100-13C)

Photo: `photos/mister-11-registres-4.jpg`.

| Address | FZ-10 | Real MiSTer | Simulation | Remark |
|---|---|---|---|---|
| 080 | 000000BA | 000000BA | 000000BA | HDELAY, identical |
| 084 | 00000076 | 00000000 | 00000000 | |
| 088 | 00000001 | 00000000 | 00000000 | |
| 100 | 0000BE3E | 0000EA89 | 0000BC9C | counter, varies |
| 104 | 0000F423 | 0000F423 | 0000F423 | reload, identical |
| 108 | 0000F7B1 | 0000FD7B | 0000FFCF | counter, varies |
| 10C | 0000FFFF | 0000FFFF | 0000FFFF | identical |
| 110, 114 | 0000FFFF | 0000FFFF | 0000FFFF | identical |
| 118 | 0000014B | 000002B6 | 000002B5 | counter, varies |
| 11C | 000003A8 | 000003A8 | 000003A8 | reload, identical |
| 120, 124 | 0000FFFD, 0000FFFF | 0000FFFD, 0000FFFF | 0000FFFD, 0000FFFF | identical |
| 130 | 0000F40A | 0000F3E3 | 0000F3EB | counter, varies |
| 134 | 0000FFFF | 0000FFFF | 0000FFFF | identical |
| 138 | 00000001 | 00000001 | 00000001 | identical |
| all others | 00000000 | 00000000 | 00000000 | |

### Real MiSTer summary (11 pages recorded)

The simulation reproduces the real MiSTer on the CPU and cel times (less than 1,6 %), on the 24 rendering CRCs and on the registers. It departs from it on three points:
- matrix math: 8 lines wrong in simulation, right on the FPGA (a signed zero, in a working copy, gives back the 13 CRCs of the MiSTer);
- CD read: about 3 ms less per request;
- free DRAM: 32768 bytes more than on the MiSTer.

Gaps of the core relative to the FZ-10, confirmed on the FPGA:
- BOUCLE: 60 VBL instead of 120 (Strahl symptom);
- cel times too short, from -3 % (stretched background) to -97 % (off-screen cel);
- CPU and memory too fast, by 2 to 16 %;
- DIVZ (2 CRC lines; console rule found, gap of at most 0,0089 and only for negative Z) and rendering R04, R21, R22;
- CD without a speed or seek model;
- VBL 59,83 Hz instead of 59,94; audio 44 100 Hz instead of about 43 964;
- MADAM and CLIO revisions, MADAM registers 00C and 040, empty MADAM addresses, CLIO 00C, 024, 03C, 084, 088.

## Cel times: first breakdown (hypotheses to be confirmed)

Costs derived from the VITESSE and CELS DETAIL pages, once the time of a `DrawCels` call is removed (CEL 1PX: 128 µs on the FZ-10, 112 µs on the MiSTer). A 320x240 cel has 76 800 pixels; in 16 bits, 38 400 source words.

| Cost | FZ-10 | Real MiSTer | Derived from |
|---|---:|---:|---|
| Read of one source word | 227 ns | not read | 16B HORS |
| Opaque pixel, 1:1 image | 89 to 97 ns | about 48 ns | PACK REPET, 16BPP - HORS |
| Opaque pixel, stretched background | 87,5 ns | 85,4 ns | CEL FOND |
| Transparent pixel walked over | 53 ns | 22 ns | PACK TRANS |
| Screen read for a blend, per pixel | 176 to 199 ns | 52 to 93 ns | DEMI - opaque |

Observations:
- On the console, the time of 16B HORS matches the read of the whole source: the off-screen cel is not skipped (the test enables neither ACSC nor ALSC). The core takes only 0,3 ms.
- On the console, an opaque pixel costs about the same at 1:1 and as a stretched background. In the core, 1:1 is almost twice as fast.
- Lead checked in simulation (next section): `MADAM_SE.sv` processes two source lines at a time (engines A and B) even when the ACE flag of the cel is 0.

## Cel engine with ACE honoured (simulation only)

Change tried on a working copy: `if (LINE_LAST && !LRFORM && SCOB_FLAG.ACE)` at the two places in `MADAM_SE.sv` where the second engine starts. All cels of these pages have ACE at 0, like those of Strahl. The core in the repository is not modified.

| Test (MED) | FZ-10 | 1109c simulated | ACE honoured | ACE / FZ-10 |
|---|---:|---:|---:|---:|
| BOUCLE X60 | **120 VBL** | 60 VBL | **120 VBL** | |
| STRAHL F+I | 17552 | 15040 | 16608 | 0,946 |
| CEL FOND | 6848 | 6672 | 6736 | 0,984 |
| CEL IMAGE | 10864 | 7936 | 9888 | 0,910 |
| CEL 16BPP | 16304 | 13104 | 14160 | 0,868 |
| CEL 8BPP | 11920 | 9648 | 10752 | 0,902 |
| CEL DEMI | 29792 | 18224 | 20704 | 0,695 |
| CEL 8X8 1K | 18288 | 14624 | 18048 | 0,987 |
| 16B VRAM | 16256 | 13568 | 14224 | 0,875 |
| 6BPP NONPK | 10768 | 8688 | 9904 | 0,920 |
| 4BPP | 9568 | 7344 | 9008 | 0,941 |
| 1BPP | 7408 | 4192 | 7200 | 0,972 |
| PACK REPET | 7120 | 3792 | 7056 | 0,991 |
| PACK TRANS | 4320 | 1808 | 3488 | 0,807 |
| 16B 1/2 | 10944 | 6960 | 8784 | 0,803 |
| 16B X2 | 9600 | 7232 | 9168 | 0,955 |
| 16B ROT45 | 15856 | 10768 | 14688 | 0,926 |
| 16B HORS | 8832 | 304 | 384 | 0,043 |
| 8BPP TRANS | 9792 | 5856 | 6704 | 0,685 |
| FOND DEMI | 22160 | 13776 | 13776 | 0,622 |
| IMAGE DEMI | 25456 | 11888 | 16464 | 0,647 |
| 16B 8X240 | 608 | 576 | 736 | 1,211 |
| 16B 320X8 | 640 | 512 | 560 | 0,875 |
| 32X32 X100 | 22896 | 19280 | 22672 | 0,990 |

Runs `ace_v` (frame 2370) and `ace_c` (frame 1920). Still to be checked: the rendering CRCs with this change, Strahl itself, and the real MiSTer. Remaining gaps: screen read for blends (DEMI), off-screen cel, transparency, and 8X240 now too slow.
