# 3DO for MiSTer

## Status

Current status is WIP/Beta

Some games have popping and noise.


## Kanji ROM
Rename the kanji.bin file to kanji.rom and place it in the game folder or parent folder.

## Arcade
Rename the 3do_arcade_saot.bin file to cd_bios.rom and place it in the game folder or parent folder.

## Official beta 
Rename the panafz1j-norsa.bin (for NTSC) or panafz1e-unencrypted.bin (for PAL) file to cd_bios.rom and place it in the game folder or parent folder.

## Changing disc (in a multi-disc game) 
When the game prompts you to change the disc, you need to save NVRAM. Then insert the next disc.

## P2i Lab fork: work in progress

This fork of [srg320's 3DO core](https://github.com/MiSTer-devel/3DO_MiSTer) is where P2i Lab tests changes against a real console, a Japanese Panasonic FZ-10. The official core is srg320's: nothing here is an official release, and the test branches are not meant for general use. Most of the code and the analysis is done with an AI assistant (Claude), then checked in simulation and on real hardware.

### What our changes bring

What a player notices. "Official" means already in srg320's core or in Main_MiSTer, and "proposed" means sent to srg320 and waiting for review. The rest is only in our test cores: the SDRAM core (branch `feat/vram-sdram`, explained below) and the core with the VRAM in the DDR3 (branch `test/kit-20261114i`). "Checked on a MiSTer" means tested on real hardware, not only in simulation.

- **VRAM in the SDRAM module.** The SDRAM core keeps the whole 1 MB of VRAM in the SDRAM module, instead of splitting it between the FPGA's block RAM and the DDR3: regular timing, and no per-game memory table. Explained in the next section. Its previous version vsd46 was checked on a MiSTer with the test disc 3.7.
- **Panasonic FZ-10 BIOS.** Games now start with it: before, the console stopped on the 3DO logo. The logo also shows without the blue background and the white lines. Checked on a MiSTer.
- **Saves.** Main_MiSTer formats a new, empty save file, so games can save from the first run. Before, Need for Speed could say "NVRAM Full". Official (MiSTer-devel/Main_MiSTer#1415).
- **Audio CDs.** The console's CD player no longer hangs (official, MiSTer-devel/3DO_MiSTer#24), and it plays audio CDs with left and right in place (proposed: MiSTer-devel/3DO_MiSTer#25, MiSTer-devel/3DO_MiSTer#27 and MiSTer-devel/Main_MiSTer#1414). Checked on a MiSTer.
- **Light guns.** Light gun games such as Mad Dog McCree and Crime Patrol can be played with PlayStation 1 GunCons on the SNAC port, or with a joystick or a mouse, with one or two guns. The gun follows measurements of two real 3DO light guns on our FZ-10. Checked on a MiSTer with a GunCon on a CRT TV. Plugging in real 3DO guns through a SNAC adapter is a later step. In the SDRAM core, the guns come after the pads in the chain, as with a Gamegun plugged into a pad (the set-up Demolition Man expects). The SDRAM core vsd57 also takes a Sega Saturn light gun on the SNAC port, for one player (in simulation).
- **Loading times.** The CD drive reads at the real console's double speed and takes the time a real drive needs to move its head, as measured on our FZ-10. Mad Dog McCree goes from the 3DO logo to its first scene in 22.5 s on our test core, 22.35 s on a real FZ-10, and about 10 s before this change (in simulation). Checked on a MiSTer.
- **Drawing speed.** The drawing engine follows the real console. Of 1690 drawing times measured on our FZ-10, 78 % are within 10 % on our test core. Before, some pictures were drawn twice as fast as on a real 3DO. On the SDRAM core vsd48, 80 % of the 898 drawing times of the test discs 3.6 and 3.7 are within 10 %, with a mean gap of 6.8 %. Some cases are still off.
- **Sound rate.** 43 960 samples a second, as measured on our FZ-10, instead of 44 100.
- **Strahl.** The intro is no longer too fast, and its narration is no longer cut. Checked on a MiSTer against a video of a real FZ-10.
- **Discs.** In multi-disc games, changing the disc works as on a 3DO. A missing or refused disc no longer leaves a black screen. Checked on a MiSTer.

### The SDRAM core: the VRAM in the SDRAM module

Branch [`feat/vram-sdram`](https://github.com/P2i-Lab/3DO_MiSTer/tree/feat/vram-sdram). Its test core, vsd57, can be downloaded from [p2i-lab.com/mister](https://p2i-lab.com/mister/), with the previous one, vsd53; the RBF is not on GitHub.

**Why.** The 3DO has 1 MB of VRAM, where it keeps its pictures and its cels. This memory has functions of its own: a serial port that sends the picture to the video, flash writes that clear a whole row, and copies of whole rows. The FPGA of the DE10-Nano has enough block RAM for only 448 KB of it (0x70000 bytes, as the comment in `3DO.sv` says), so the official core keeps three windows of the VRAM, plus its last 16 KB, in block RAM, and the rest in the DDR3. The DDR3 is shared with Linux and the MiSTer scaler, and its delays vary: when the video or the drawing engine waits for it, the console pauses, and data that come too late can show on the screen. So each game's screens must fall inside the block-RAM windows. A table in `3DO.sv` moves the windows for the games that keep their screens elsewhere: 11 games in the official core (upstream 2cf0583), 15 in our test core 1114i. The other games get the default windows.

**What changes.** The whole megabyte of VRAM moves to banks 2 and 3 of the SDRAM module, next to the main memory: the same module as the official core, no extra hardware. The core is alone on that memory, so its timing is regular and predictable, and the per-game window table is not used any more. Since vsd49, `3DO.sv` has no game list at all: the DSP pauses with the console in every game, not only in the five games of the list.

**The challenge.** The SDRAM is slower than block RAM, and a row copy done in the SDRAM would take about 14 µs: the copies of a frame would no longer fit in the vertical blank. `rtl/vram.sv` is rewritten (its header describes the design):
- Reads and writes of the main bus use the DRAM slots of `rtl/sdram.sv`, in banks 2 and 3.
- Serial port: a read transfer only records which row each half of the serial register stands for. The words are read ahead of the display (16 pairs ahead) and stay valid until that half row is written.
- A row cache in the block RAM the VRAM no longer needs (for each of the two VRAM chips, 7 × 32 rows of 256 pairs, one valid bit per block of 32 pairs). The video takes a cached block at once. Masked write transfers and flash writes fill the row in the cache only, in about 40 clocks, as fast as on the console; the dirty row is written to the SDRAM later.
- The CPU's row copies have a register of their own, apart from the video's serial register. While a copy or a flash write is still running, the CPU's next VRAM access waits off the bus, so the video transfers stay on time. Since vsd53, MADAM's display-list reads and its memory refresh no longer wait for a copy: while MADAM reads the display list or the picture lines, no copy fill starts, and those reads pass at once when they touch neither the copied row, the written row nor the cache slot being filled.
- `rtl/sdram.sv` gets a block engine for these transfers. The console stops while the engine owns the SDRAM, and the cel engine counts those stopped cycles in its pace (`CE_RP`): a cel takes the time of the timing model, never less.

**How it was checked.** A simulator of the whole system (Verilator, with an SDRAM model that checks the SDRAM timing rules) ran our test discs and Strahl's intro on every version. It showed six defects, all corrected:
- black line pairs in Strahl's animated sequence, before the cache;
- dots on Strahl's title screen, at column 0 of the VRAM rows: the CPU's serial-port cycles were taken as main-bus writes (seen on a MiSTer with vsd43, corrected in vsd44);
- a burst of late words when the screen changes (corrected in vsd46);
- stray dots in the console's audio CD player: a console write seen while the console was stopped in the middle of a memory cycle was lost (corrected in vsd50, checked on a MiSTer);
- the menus of Crime Patrol and Mad Dog McCree jumped by one line and lost colour words: these games copy their background at the top of each frame from rows that are not in the cache, and MADAM's display-list reads waited behind those copies (corrected in vsd53, checked on a MiSTer);
- a few stray pixels at the start of the first line of the picture, then the right line shifted to the right (11 pixels in the menu of Mad Dog McCree, one frame in two): just before the picture, the palette's colour words come down the same serial path as the pixels; when the SDRAM delayed them, they came after the line had started and CLIO wrote them as pixels. Now only a word received in the visible part of the line becomes a pixel (corrected in vsd54).

The 24 reference drawings of the test disc give the same CRCs as on the FZ-10. On a real MiSTer, vsd46 ran the 640 cases of the test disc 3.7 with a mean gap of 0.3 % to its simulation.

**Drawing speed.** vsd47 and vsd48 also change the cel engine, from the disc 3.7 measurements of narrow cels:
- 16-bit cels ask for source words when 2 are left in the buffer, not 3;
- refills go before the draw;
- a draw burst stops after 16 pixels for unpacked cels, except solid cels whose source pixel covers more than 1.125 screen pixels, such as fills;
- 16-bit rows without ACE wait 80 ns before the draw restarts.

vsd50 adds a pace of its own for turned or sheared 16-bit cels without ACE, from the measurements of the discs 3.6 and 3.7.

vsd55 adds two rules for turned cels, from the same measurements, which showed some turned cels drawn up to 20 % too fast: a turned, unpacked 16-bit cel with ACE pays a second memory access when the two drawing engines write on the same line, which happens more often as the angle nears 90 degrees (cels blended with the picture already paid it); and a turned cel with ACE, or of fewer than 16 bits per pixel, whose source pixel covers more than one and up to two screen lines (scaled 1.3 to 1.7 times, for example) costs a little more as it grows. Over the 1112 drawing times of the disc 3.6 sequence, the cases more than 10 % too fast go from 17 to 14 (simulation).

With vsd48, over 898 drawing times of the test discs 3.6 and 3.7, the mean gap to the FZ-10 is 6.8 % (vsd46: 7.6 %), and 80 % are within 10 % (simulation).

**Also in this branch.** The light guns come after the pads in the controller chain, as with a Gamegun plugged into a pad. That is the set-up the Gamegun's documentation asks for Demolition Man. In Demolition Man, pressing the gun's calibration button during the intro freezes the game, on a real FZ-10 too. Since vsd57, the Input page is simpler, a box can show which player each controller is, and a Sega Saturn gun works on the SNAC port: see [Gamepads and light guns](https://github.com/P2i-Lab/3DO_MiSTer/blob/feat/vram-sdram/README.md#gamepads-and-light-guns) in the README of the branch.

Commits on top of `test/kit-20261114i`:
- `12a1806`: light guns after the pads;
- `00ed4dc`: VRAM in the SDRAM module (test core vsd46);
- `76428d8`: narrow-cel timing (vsd47);
- `6638b5a`: no 16-pixel limit for stretched solid cels (vsd48);
- `ee58651`: no game list, the DSP pauses with the console in every game (vsd49);
- `7019f85`: console writes kept when the console stops mid-cycle (vsd50);
- `22f0009`: pace of turned 16-bit cels (vsd50);
- `e6e2d43`: the display list and the refresh no longer wait for a row copy (vsd53);
- `6f5bc79`: only words of the visible part of the line become pixels (vsd54);
- `4d78654`: two rules for turned cels (vsd55);
- `a3cc3d4`: gamepad menu, controller box and Saturn gun (vsd57).

Later versions come as new commits on top.

### Sent to the official projects

- CLIO RandSample and RandSeed: merged (MiSTer-devel/3DO_MiSTer#24).
- Save files that hold no NVRAM volume are formatted by Main_MiSTer: merged (MiSTer-devel/Main_MiSTer#1415).
- Audio CD playback (MiSTer-devel/3DO_MiSTer#27, MiSTer-devel/Main_MiSTer#1414) and DSP input FIFO reads (MiSTer-devel/3DO_MiSTer#25): open.
- Notes for srg320 in MiSTer-devel/3DO_MiSTer#23: CD error status, disc changes in multi-disc games, ARM behaviour, FPGA timing, Quarantine, Strahl's screens outside the BRAM blocks.

### Measured on a real FZ-10

Homebrew benchmark discs (P2i Lab bench 2.3 to 3.6) measured about 2500 cel engine times, the matrix math, the ARM CPU, memory and video timing, the CD drive, the control port and the 3DO light gun. All the values, our reading of them and the bench sources are in [fz10-measurements](https://github.com/P2i-Lab/3DO_MiSTer/tree/report/fz10-measurements) on the `report` branch; the disc image is attached to the release [bench-disc-3.6](https://github.com/P2i-Lab/3DO_MiSTer/releases/tag/bench-disc-3.6).

### Test branches

- `feat/vram-sdram`: the SDRAM core: the VRAM in the SDRAM module, the timing of narrow cels and fills from the disc 3.7 measurements, the light guns after the pads, the Saturn gun on the SNAC port. Its test core vsd57 is on [p2i-lab.com/mister](https://p2i-lab.com/mister/).
- `test/kit-20261114i`: our test core with the VRAM in the DDR3, with all the core changes above except the SDRAM ones.
- `test/mesures-20261110`: the core corrected from the first measurements (cel engine, CD drive, memory).
- `feat/lightgun`: the 3DO light gun (control port device 0x4D), aimed with a joystick, a mouse, or PlayStation GunCons on the SNAC port. Tested with a GunCon on a CRT (Mad Dog McCree, Crime Patrol).
- `pr/pause-meter`: a debug option that shows how long the console waits for the DDR3.
