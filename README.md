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

What a player notices. "Official" means already in srg320's core or in Main_MiSTer, and "proposed" means sent to srg320 and waiting for review. The rest is only in our test core, branch `test/kit-20261114i`. "Checked on a MiSTer" means tested on real hardware, not only in simulation.

- **Panasonic FZ-10 BIOS.** Games now start with it: before, the console stopped on the 3DO logo. The logo also shows without the blue background and the white lines. Checked on a MiSTer.
- **Saves.** Main_MiSTer formats a new, empty save file, so games can save from the first run. Before, Need for Speed could say "NVRAM Full". Official (MiSTer-devel/Main_MiSTer#1415).
- **Audio CDs.** The console's CD player no longer hangs (official, MiSTer-devel/3DO_MiSTer#24), and it plays audio CDs with left and right in place (proposed: MiSTer-devel/3DO_MiSTer#25, MiSTer-devel/3DO_MiSTer#27 and MiSTer-devel/Main_MiSTer#1414). Checked on a MiSTer.
- **Light guns.** Light gun games such as Mad Dog McCree and Crime Patrol can be played with PlayStation 1 GunCons on the SNAC port, or with a joystick or a mouse, with one or two guns. The gun follows measurements of two real 3DO light guns on our FZ-10. Checked on a MiSTer with a GunCon on a CRT TV. Plugging in real 3DO guns through a SNAC adapter is a later step.
- **Loading times.** The CD drive reads at the real console's double speed and takes the time a real drive needs to move its head, as measured on our FZ-10. Mad Dog McCree goes from the 3DO logo to its first scene in 22.5 s on our test core, 22.35 s on a real FZ-10, and about 10 s before this change (in simulation). Checked on a MiSTer.
- **Drawing speed.** The drawing engine follows the real console. Of 1690 drawing times measured on our FZ-10, 78 % are within 10 % on our test core. Before, some pictures were drawn twice as fast as on a real 3DO. Some cases are still off.
- **Sound rate.** 43 960 samples a second, as measured on our FZ-10, instead of 44 100.
- **Strahl.** The intro is no longer too fast, and its narration is no longer cut. Checked on a MiSTer against a video of a real FZ-10.
- **Discs.** In multi-disc games, changing the disc works as on a 3DO. A missing or refused disc no longer leaves a black screen. Checked on a MiSTer.

### Sent to the official projects

- CLIO RandSample and RandSeed: merged (MiSTer-devel/3DO_MiSTer#24).
- Save files that hold no NVRAM volume are formatted by Main_MiSTer: merged (MiSTer-devel/Main_MiSTer#1415).
- Audio CD playback (MiSTer-devel/3DO_MiSTer#27, MiSTer-devel/Main_MiSTer#1414) and DSP input FIFO reads (MiSTer-devel/3DO_MiSTer#25): open.
- Notes for srg320 in MiSTer-devel/3DO_MiSTer#23: CD error status, disc changes in multi-disc games, ARM behaviour, FPGA timing, Quarantine, Strahl's screens outside the BRAM blocks.

### Measured on a real FZ-10

Homebrew benchmark discs (P2i Lab bench 2.3 to 3.6) measured about 2500 cel engine times, the matrix math, the ARM CPU, memory and video timing, the CD drive, the control port and the 3DO light gun. All the values, our reading of them and the bench sources are in [fz10-measurements](https://github.com/P2i-Lab/3DO_MiSTer/tree/report/fz10-measurements) on the `report` branch; the disc image is attached to the release [bench-disc-3.6](https://github.com/P2i-Lab/3DO_MiSTer/releases/tag/bench-disc-3.6).

### Test branches

- `test/kit-20261114i`: our latest test core, with all the core changes above.
- `test/mesures-20261110`: the core corrected from the first measurements (cel engine, CD drive, memory).
- `feat/lightgun`: the 3DO light gun (control port device 0x4D), aimed with a joystick, a mouse, or PlayStation GunCons on the SNAC port. Tested with a GunCon on a CRT (Mad Dog McCree, Crime Patrol).
- `pr/pause-meter`: a debug option that shows how long the console waits for the DDR3.
