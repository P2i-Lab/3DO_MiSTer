# 3DO core: fixes from the P2i-Lab fork

Report for Sergiy Dvodnenko (srg320), November 2026.

These changes were written with Claude Code (Anthropic's AI coding assistant) and tested by Harry (P2i Lab). Each change was first checked in simulation (the whole core in Verilator, driven by the real Main_MiSTer 3DO code (`support/3do`), or a testbench) and compared with Opera (libretro) where possible. The "Checked" column of the summary says which ones were then tested on a DE10-Nano with the FZ-1 (U) BIOS.

Everything is on the fork, [P2i-Lab/3DO_MiSTer](https://github.com/P2i-Lab/3DO_MiSTer), with one branch per change based on your latest `main` (b419cbb). The Main_MiSTer changes are provided as patches against its latest `master` (cc47bad).

## Update, 8 November 2026

Your `main` is now 2cf0583, with MiSTer-devel/3DO_MiSTer#24, your ARM fixes (4970e53) and the DRAM sizes (3ec27de). Our branches are rebased on it. The sections after this update still describe them as of 7 November.

| Branch | Now |
|---|---|
| `fix/clio-random` | Merged as MiSTer-devel/3DO_MiSTer#24. |
| `pr/dsp-fifo-read` | Open as MiSTer-devel/3DO_MiSTer#25. It still merges cleanly on 2cf0583. |
| `pr/cd-audio` | Replaced by MiSTer-devel/3DO_MiSTer#27 (branch `cd-audio`, `rtl/3DO_CDROM.sv` only) and, for Main, by MiSTer-devel/Main_MiSTer#1414. |
| `pr/nvram-format` | Replaced by MiSTer-devel/Main_MiSTer#1415: Main formats a save file that holds no NVRAM volume, and the core does not change. |
| `fix/cd-error-status`, `pr/cd-disc-change`, `pr/disc-swap-reboot`, `pr/timing` | Rebased. Their commits did not change. |
| `pr/pause-meter` | Rebased. The OSD option moved to status bit 22, because Sysmem now uses bits 20 and 21. |
| `pr/arm6-cpu` | Rebased and reduced to the two points that neither 4970e53 nor canxin121's PRs cover: an immediate LSR #0 shifts by 32, and unaligned LDR and SWP rotate the word by 8 × A[1:0]. It also removes the duplicate `GR_R15` of the simulation-only block, which Verilator rejects. |
| `pr/game-table` | New. Quarantine (US) (RE2), disc id 356B12E2, gets the VRAM windows of Quarantine (US), {8,6,C}. This does not remove the black screen described in MiSTer-devel/3DO_MiSTer#23. |

**ARM6 tests on 2cf0583 with `pr/arm6-cpu`.**
- `sim/arm6`: 8 of the 12 programs pass. The other four need canxin121's PRs: `t_msr` needs MiSTer-devel/3DO_MiSTer#18, `t_irq` and `t_swi` need #19 (their setup uses `MSR CPSR_flg, #imm`), and case 22 of `t_regress` needs #17. The branch's `sim/arm6/README.md` lists them.
- Fuzzing, with the same 1500 random programs without `MSR #imm` as in our ARM6 note on MiSTer-devel/3DO_MiSTer#23: 214 differ, against 396 on 2cf0583 alone. In 211 of them the instruction that makes the results differ is an `MSR SPSR_flg` (#18), and in the other 3 it is a logical shift by a register (#17).

**Whole-core check.** `test/integration-20261108` merges all these branches with #25 and #27. We ran it in the system simulation with Main_MiSTer `master` cc47bad plus MiSTer-devel/Main_MiSTer#1414, #1415 and patches 2 and 3 below, which still merge cleanly. We compared it with `test/int-20261107c`, the 7 November test build.
- Doom: the 29 pictures taken over the first 900 frames are identical to those of the 7 November test build.
- FZ-1 audio CD player: it reads 48 different RandSample values, and its sound is identical, sample for sample, to that of the 7 November test build.
- Without a disc, and with the Warner Music Group disc, the BIOS logo appears. On 2cf0583 alone the screen is still black at frame 800.
- BrainDead 13: inserting disc 1 at the "CHANGE DISC PLEASE" screen of disc 2 restarts the console, which then shows the game's opening screen.

The rebased branches have not been built or tested on hardware yet.

## Summary

| Branch | Change | Size | Checked |
|---|---|---|---|
| `fix/clio-random` | CLIO RandSample (0x3C) and RandSeed (0x38) implemented: the BIOS audio CD player no longer hangs | 1 file, +17 | sim, hardware |
| `pr/dsp-fifo-read` | DSP input FIFO reads return the head of the FIFO: the CD player plays left and right in place | 2 files, +39 −19 | sim, hardware (6 games) |
| `fix/cd-error-status` | CD drive: a failed spin-up no longer leaves the drive in error (black screen without a disc or with an unbootable disc) | 1 file, +6 −1 | sim, hardware |
| `pr/cd-disc-change` | CD-ROM: disc changes reported while the console runs (issue #9) | core + testbench | testbench, sim |
| `pr/disc-swap-reboot` | Watchdog reset: a disc swap restarts the console as a 3DO does (builds on `pr/cd-disc-change`) | core + testbench | testbench, sim, hardware |
| `pr/cd-audio` | Audio CD playback (needs the Main patches) | core, testbenches, tool | testbenches, sim, hardware |
| `pr/nvram-format` | A save file without a volume label is loaded as a blank NVRAM | core, testbench, tool | testbench |
| `pr/timing` | MADAM: two timing paths shortened (CPU SEQ, REG_ZERO) | 2 files, +18 −5 | builds |
| `pr/arm6-cpu` | ARM6 fixes, including unaligned LDR/SWP rotation | core + tests + fuzzer | testbench, fuzzer |
| `pr/pause-meter` | Optional debug overlay: memory pauses per frame | core + testbench | testbench |

Merged together, these branches give exactly the tree of `test/integration-20261107b`, the build tested on hardware. Two of them conflict if merged one after the other:
- `rtl/3DO_CDROM.sv`: `pr/cd-audio` against `pr/cd-disc-change` and `fix/cd-error-status`;
- `files.qip`: `pr/nvram-format` against `pr/pause-meter` (one added line each).

We will rebase whichever comes second, so merge them in any order you like.

## 1. CLIO RandSample — `fix/clio-random`

**Symptom.** The BIOS audio CD player hangs right after it reads the disc. With the FZ-1 BIOS the screen stays on "PLAY 1 0:01", which is the forum's "1 track, 1 second". With the FZ-10 BIOS it shows STOP without its title. The pad is ignored.

**Cause.** CLIO 0x38 (RandSeed) and 0x3C (RandSample) were not implemented and read 0. The kernel's `ReadHardwareRandomNumber` reads RandSample twice and combines the values, so it always returned 0. When it starts, the player builds a random track order: it draws numbers until it has every track and rejects the ones out of range. With 0 it never gets one, and loops forever.

**Change.** RandSample returns a 32-bit LFSR (x^32+x^22+x^2+x+1) that steps on each CPU read of 0x3C, as Opera returns a new value on each read (changed after review, merged in MiSTer-devel/3DO_MiSTer#24; the first version stepped every clock). How the hardware makes the value is not documented. RandSeed is stored and read back.

**Checked.**
- Simulation: with the first version, the FZ-1 and FZ-10 players start and play; with the merged version, the FZ-1 player reads 48 different values and plays the test CD.
- Hardware, first version: the FZ-1 player plays a 5-track test CD.

## 2. DSP input FIFO reads — `pr/dsp-fifo-read`

**Symptom.** In the CD player, left and right were swapped. With a test CD holding 440 Hz on the left and 660 Hz on the right, the core played 660 Hz on the left.

**Cause.** A DSP read of an input FIFO (bump 0x0F0-0x0FC, read 0x070-0x07C) returned `EIFIFO_BUF`, which the read itself had loaded, so the DSP got the word before the head. The player's `fixedstereo16swap.dsp` bumps the left sample and then the right one of each stereo frame.

**Change.** A read returns the head, `EIFIFO_Q`, in the DSP cycle itself, and two cases follow Opera:
- **CPU-supplied value.** A value written by the CPU to 0x070+n is returned by the next read of channel n, and the FIFO does not move (Opera: `CPUSupply`). The audio folio supplies a value before it starts a stream. Popping there would lose the stream's first word: Psychic Detective's first sound lost its header word (ff00) and its decoder produced 1.3 s of noise at full scale.
- **Empty FIFO.** A read of an empty FIFO returns 0 and pops nothing. The head slot of an empty FIFO holds a word one lap old, and Opera returns 0 for a channel without data.

`CLIO_DSP_FIFO` now also counts correctly a write and a read in the same cycle on an empty or a full FIFO. The count used to stay one off from the pointers.

We also hit a pitfall on the way. A first version copied the head into `EIFIFO_BUF` between the DSP cycles. The FIFO is an MLAB written in the cycle after the CE_R edge that registers the write. So when the FIFO was empty, that copy could catch the old content of the slot, which made crackles on hardware only. The final version reads in the DSP cycle, where the MLAB is stable.

**Checked.**
- Simulation:
  - the FZ-1 and FZ-10 players play left and right in place;
  - Psychic Detective's intro (56 s) is sample-exact with your previous read;
  - Wing Commander III's opening (49 s) has no cut-out or click against Opera.
- Hardware (FZ-1 BIOS): Psychic Detective, Wing Commander III, Need for Speed, Super Street Fighter II Turbo and Twisted play cleanly, and the audio CD comes out with left and right in place.

## 3. CD drive error status — `fix/cd-error-status`

**Symptom.** After the 3DO logo, the screen stayed black: forever without a disc, and about 6 s longer with a disc the BIOS refuses (an unencrypted disc such as the Warner Music Group sampler, or the BattleSport, Blade Force and Killing Time betas).

**Cause.** A spin-up (0x02) without a disc set the error bit and cleared READY, and nothing set READY again. The end of the command also cleared the error code (0x15) before the Read Error command (0x82) could return it, so the BIOS kept retrying.

**Change.** Each command clears STATUS[4] (error) and sets STATUS[0] (ready), and a command that fails sets them again. The error code stays available for 0x82. The replies are now the same as Opera's: 02 gives `02 10`, and 82 gives `82 15×8 01`.

**Checked.**
- Simulation:
  - Warner Music Group: the R·E·A·L logo and then the CD tray screen, at the same points as in Opera;
  - without a disc, the logo now appears at frame 600.
- Hardware: Warner Music Group goes back to the BIOS idle screen.

## 4. Disc changes while the console runs — `pr/cd-disc-change` (issue #9)

**Cause.** Mounting a new image made `CDD_IS_LOADED` fall and rise, but only the rise was used. The drive never reported "door open / no disc". `CDMDCHG` stayed set until a drive reset, so a second disc gave no new edge. The OSA POLL bit 7 (media access) was only set at reset, and the code that re-set it was commented out.

**Change.**
- `CDMDCHG` is now a one-clock pulse when the disc info of a new image completes while the console runs.
- The drive status goes to 0x01 when the disc goes and to 0xC1 when it comes back.
- OSA sets POLL[7] on the pulse, so the OS sees the media access, as in Opera.
- The per-game settings (VRAM windows, DSP pause) are applied only while the console is in reset, so a hot swap does not change the VRAM mapping in the middle of a game.

**Checked.** `sim/cdrom` testbench: it passes with the change and fails on the previous code.

## 5. Watchdog reset on a disc swap — `pr/disc-swap-reboot` (builds on 4)

**Symptom.** After a disc swap the screen stayed black: the CPU loops at 0xA10-0xA18.

**Cause.** When the running title does not accept another disc, the boot ROM's `dipir()` hardboots the console. That happens when the kernel flags have `_3DO_LUNCH` without `DATADISCOK` or `NORESETOK`, or when the new disc lacks `VOLUME_FLAGS_DATADISC`. It writes the CLIO WatchDog (0x2C = 0x0B), then sets ADBIO 0x88 (WATCHDOG | EN). Every disc we checked has those flags at 0, so on a 3DO a disc swap is a console restart, and multi-disc games resume from their save. The core had no watchdog reset. Opera has none either.

**Change.**
- `WDOG_RST = ADBIO_D[3] & ADBIO_O[3]` resets the console.
- The drive keeps the disc and status 0xC1 through that reset (`HOLD_DISC`).
- The per-game settings of the new disc apply at the restart (`disc_new`).
- Psychic Detective discs 2 (3A43EB1B) and 3 (0EA661F9) are added to the table, with the windows of disc 1.

**Checked.**
- Simulation: a swap at frame 700 brings the 3DO logo at frame 900 and the disc 2 title at frame 1020.
- Hardware, Psychic Detective: loading disc 2 during the intro restarts the console, which shows "WRONG DISK: INSERT DISK 1 TO CONTINUE", as expected without a save.

**About issue #9.** You suggested sharing one save across the discs, with the Main resetting the console on a disc change. That works with this branch too. This branch adds the path a real 3DO takes when the Main does not reset (Main patch 3 below), and keeps the disc across the restart. We would like your view on which behaviour you prefer.

## 6. Audio CD playback — `pr/cd-audio` (with Main patches 1)

The Portfolio driver plays CD-DA without any PLAY command. It sends a MODE SET (0x09, page 0: density, block length, flags) and then READ DATA (0x10) of 2352-byte blocks, or 2448 with subcode, through the data FIFO.

**Core (`rtl/3DO_CDROM.sv`).**
- MODE SET page 0 is stored, and the buffer grows to 4 KB.
- A sector ends when the transfer from Main ends.
- READ forwards the requested format and subcode to Main and takes a 24-bit block count.
- READ SUBQ (0x87) is added.
- Reply lengths follow the FZ-1 guide, Portfolio and Opera: ABORT 1 byte, most commands 7. The parser used to lose sync on 01/04/05/0B.
- Unknown commands answer with an error (0x17).

**Main (patches 1).**
- Sector format and subcode requested by the core, raw sectors with the 96 subcode bytes (Q in bit 6, CRC), and TOC control bits.
- Samples in disc order: BIN and WAVE as they are, MOTOROLA and CHD swapped. The player's `fixedstereo16swap.dsp` byte-swaps them itself.
- CD-DA delivered at the speed of the disc, 75 sectors per second. The FZ-1 player tracks the DSP and adjusts the pitch; unpaced data overran its ring buffer.

**Checked.**
- `sim/cdaudio` testbench (15 steps) and a host test of the Main code.
- System simulation: FZ-1 and FZ-10 players.
- Hardware: a 5-track test CD made for this. Left and right are in place and tracks advance. A phone recording of the metronome track was analysed: 58 clicks out of 58, all within 0.23 ms, speed exact, no dropout.
- One oddity: the player's level meter lights the right bar for left-only sound, and Opera does the same with this disc.

## 7. Blank NVRAM for a save without label — `pr/nvram-format`

**Symptom.** Need for Speed reports "NVRAM Full: Configuration not saved!". This is also reported on the forum with the official core.

**Cause.** Main serves a formatted blank save only when the `.sav` file does not exist. An existing file without a volume label (empty, foreign or corrupted) is loaded as is. The FZ-1 (U) BIOS does not format it, unlike the FZ-10 (Anvil). Opera formats a blank NVRAM since its PR #216.

**Change.** `rtl/nvram_fmt.sv` reads the label from the save data. If it is not valid, the NVRAM gets Main's blank in sector 0 and zeros elsewhere.

**Checked.** `sim/nvram` testbench: 8 save files, all pass; your previous code fails the 6 without a label. Not yet checked on hardware.

## 8. Timing — `pr/timing`

The worst clk_mem paths of every build ran from the ARM6 decode, through the combinational SEQ, to MADAM_CPUIF START, then MADAM_AG, then the ddram, sdram and VRAM registers (10 ns).
- **CPU SEQ.** The CPU only advances at the end of PHASE2, and START is masked for at least two clk_sys cycles afterwards. So START can use SEQ delayed by one clk_sys (`SEQ_R`) without changing behaviour.
- **REG_ZERO.** REG_ZERO is tested after its register.

The first integration build with these changes met timing on every clock. Placement still decides some paths, such as MADAM_DMA_STACK to the ddram busy flags. For test builds we compile three seeds and keep one that meets timing.

## 9. ARM6 — `pr/arm6-cpu`

These fixes cover issues #10, #11 and #12 and more:
- shifter carry-out;
- immediate LSR/ASR #32;
- MSR field masks for CPSR and SPSR, and MSR immediate decode;
- LSL/LSR by a register holding 64 or more;
- unaligned LDR and SWP rotated by 8 × A[1:0], as on the ARM60.

**Tests.**
- `sim/arm6`: a Verilator testbench, self-checking assembly tests and the exact repro images of the issues. 12 pass; your previous code fails as the issues describe.
- Interrupt and SWI tests.
- Differential fuzzing against Unicorn: 5000 programs, also with random interrupts, no mismatch.

canxin121's PRs #14-#19 fix most of the same bugs. Only the unaligned load rotation is missing there. Take whichever you prefer; our tests can check either.

## 10. Pause meter — `pr/pause-meter` (optional)

An OSD option, "Pause meter", draws five bars per frame over the picture:
- the time the system was paused;
- the part of it waiting on the DDR3;
- the part waiting on the VRAM;
- the part waiting on the DDR3 I/O;
- the longest single pause, with marks at 90 and 181 µs.

The 8-deep DSP input FIFOs are not refilled during a pause, so a long pause can be heard. The meter is a debugging aid: drop it if you do not want it in releases.

## 11. Whole-core simulation — `feat/system-sim`

`sim/system` is a "virtual MiSTer". It runs the whole core in Verilator with the real `support/3do` code of Main_MiSTer, and writes PNG and WAV files.

**Options.**
- Pad presses, disc swaps, OSD reset.
- DDR3 latency jitter and spikes.
- Tracers: CD commands, CLIO and MADAM register reads, the DSP's FIFO reads.
- FIFO underrun counters and a CPU PC histogram.

Most of the findings above were found and checked with it. It is not meant for the core's releases, but we can submit it if it is useful to you.

## Main_MiSTer patches

These are in `main-patches/` against Main_MiSTer `master` cc47bad. They apply cleanly and change only `support/3do`.

1. `1-cd-audio` (3 patches): audio CD support, samples in disc order, sectors at the speed of the disc. Needed with `pr/cd-audio`.
2. `2-reset-keeps-disc`: the reset button and the OSD Reset start the game again. Before, the drive lost the disc on reset and the BIOS showed the CD tray screen, with the current core too.
3. `3-disc-swap`: a disc of the same game is swapped without a reset, so the console restarts through the watchdog as a 3DO does (needs `pr/disc-swap-reboot`). This changes how Main loads a disc, so we would follow your preference from issue #9.

## Hardware tests

These were run on a MiSTer (DE10-Nano) with an analog output to a CRT, the FZ-1 (U) BIOS and the 6–7 November 2026 test builds.

| Test | Result |
|---|---|
| OSD Reset during Doom | The game restarts. |
| Audio CD player | Plays, with left and right in place; tracks advance; metronome recording analysed. |
| Disc swap, Psychic Detective | The console restarts; disc 2 asks for disc 1, as expected without a save. |
| Warner Music Group (unencrypted) | Back to the BIOS idle screen instead of a black screen. |
| Psychic Detective, Wing Commander III, Need for Speed, Super Street Fighter II Turbo, Twisted | Sound clean, no cut-out. |
| Timing | The tested build met setup and hold on every clock (seed 3). |

## Open questions

- With the user's FZ-10 (U) BIOS, no game boots in the simulation. Your official core does the same, so it is either a simulation artefact or an old issue. It has not been tested on hardware yet.
- The DSP keeps running during system pauses while its FIFOs are not refilled: `NEED_DSP_PAUSE` covers this per game. The pause meter can tell whether some games still underrun on hardware.
- Is the CD player's level meter swapped on a real FZ-1 too? It is swapped in Opera as well.
