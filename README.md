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

### Sent to the official projects

- CLIO RandSample and RandSeed: merged (MiSTer-devel/3DO_MiSTer#24).
- Save files that hold no NVRAM volume are formatted by Main_MiSTer: merged (MiSTer-devel/Main_MiSTer#1415).
- Audio CD playback (MiSTer-devel/3DO_MiSTer#27, MiSTer-devel/Main_MiSTer#1414) and DSP input FIFO reads (MiSTer-devel/3DO_MiSTer#25): open.
- Notes for srg320 in MiSTer-devel/3DO_MiSTer#23: CD error status, disc changes in multi-disc games, ARM behaviour, FPGA timing, Quarantine, Strahl's screens outside the BRAM blocks.

### Measured on a real FZ-10

Homebrew benchmark discs (P2i Lab bench 2.3 to 3.6) measured about 2500 cel engine times, the matrix math, the ARM CPU, memory and video timing, the CD drive, the control port and the 3DO light gun. A report with all the values is being prepared for this repository.

### Test branches

- `test/mesures-20261110`: the core corrected from the first measurements (cel engine, CD drive, memory).
- `feat/lightgun`: the 3DO light gun (control port device 0x4D), aimed with a joystick, a mouse, or PlayStation GunCons on the SNAC port. Tested with a GunCon on a CRT (Mad Dog McCree, Crime Patrol).
- `pr/pause-meter`: a debug option that shows how long the console waits for the DDR3.
