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

## Light gun
Input > Light gun: 1 or 2 guns, plugged after the pads, as a Gamegun plugged into a pad (console - pad - gun 1 - gun 2); with no pad the guns come first.

With Gun input "MiSTer", a gun is aimed with the left stick of a joystick (light guns that MiSTer sees as joysticks,
or a stick) or with the mouse (Gun 1 aim, Gun 2 aim).

| Gun | Joystick | Mouse | GunCon |
|---|---|---|---|
| Trigger | A | left button | trigger |
| Reload (shot off the screen) | B | right button | B |
| Calibration button | C | middle button | A |

Aiming off the picture (the extreme values of the stick) is also a shot off the screen. The pads use the
joysticks that do not aim a gun, in order (with gun 1 on joystick 1, pad 1 is joystick 2). The flightstick and
the 3DO mouse are off while guns are used. Crosshairs: gun 1 red, gun 2 green (Input > Gun crosshair).

With Gun input "GunCon (SNAC)", the guns are Namco GunCons (PlayStation) on a PlayStation SNAC adapter (port 1:
gun 1, port 2: gun 2), aimed at a CRT fed by the analog video output (15 kHz). The core sends the composite sync
to the GunCon on pin 6 of the adapter, as the PlayStation core does. As with the real 3DO gun, a GunCon sees
nothing where the picture is dark. GunCon X adjust moves the aim of both GunCons left (negative) or right
(positive) by 2 pixels a step, from -32 to +30 pixels. 0 is the place measured with a real GunCon on a CRT (the
crosshair on the aim at the centre and at both edges); another GunCon or screen may want a step or two: aim at
a precise point of a still bright picture (the bullseye of a game's calibration screen, say) and pick the value
that puts the crosshair on it. The 3DO gun reports the adjusted aim, so games see the same shift (their own
calibration takes care of any offset).

Gun sensor (emulated guns): "Ideal" sees the picture wherever the gun is aimed; "Picture" sees, as the real 3DO
gun, only the bright places of the picture within its field (a disc about 25 lines across around the aim): the gun
reports the first bright place it meets, and a shot at a dark place is not seen.

## P2i Lab fork

This branch of P2i Lab's fork of [srg320's 3DO core](https://github.com/MiSTer-devel/3DO_MiSTer) holds the SDRAM core. The official core is srg320's: this is a test core. The fork's [main README](https://github.com/P2i-Lab/3DO_MiSTer#p2i-lab-fork-work-in-progress) lists all our changes.

### The SDRAM core: the VRAM in the SDRAM module

Branch [`feat/vram-sdram`](https://github.com/P2i-Lab/3DO_MiSTer/tree/feat/vram-sdram). Its test core, vsd53, can be downloaded from [p2i-lab.com/mister](https://p2i-lab.com/mister/); the RBF is not on GitHub.

**Why.** The 3DO has 1 MB of VRAM, where it keeps its pictures and its cels. This memory has functions of its own: a serial port that sends the picture to the video, flash writes that clear a whole row, and copies of whole rows. The FPGA of the DE10-Nano has enough block RAM for only 448 KB of it (0x70000 bytes, as the comment in `3DO.sv` says), so the official core keeps three windows of the VRAM, plus its last 16 KB, in block RAM, and the rest in the DDR3. The DDR3 is shared with Linux and the MiSTer scaler, and its delays vary: when the video or the drawing engine waits for it, the console pauses, and data that come too late can show on the screen. So each game's screens must fall inside the block-RAM windows. A table in `3DO.sv` moves the windows for the games that keep their screens elsewhere: 11 games in the official core (upstream 2cf0583), 15 in our test core 1114i. The other games get the default windows.

**What changes.** The whole megabyte of VRAM moves to banks 2 and 3 of the SDRAM module, next to the main memory: the same module as the official core, no extra hardware. The core is alone on that memory, so its timing is regular and predictable, and the per-game window table is not used any more. Since vsd49, `3DO.sv` has no game list at all: the DSP pauses with the console in every game, not only in the five games of the list.

**The challenge.** The SDRAM is slower than block RAM, and a row copy done in the SDRAM would take about 14 µs: the copies of a frame would no longer fit in the vertical blank. `rtl/vram.sv` is rewritten (its header describes the design):
- Reads and writes of the main bus use the DRAM slots of `rtl/sdram.sv`, in banks 2 and 3.
- Serial port: a read transfer only records which row each half of the serial register stands for. The words are read ahead of the display (16 pairs ahead) and stay valid until that half row is written.
- A row cache in the block RAM the VRAM no longer needs (for each of the two VRAM chips, 7 × 32 rows of 256 pairs, one valid bit per block of 32 pairs). The video takes a cached block at once. Masked write transfers and flash writes fill the row in the cache only, in about 40 clocks, as fast as on the console; the dirty row is written to the SDRAM later.
- The CPU's row copies have a register of their own, apart from the video's serial register. While a copy or a flash write is still running, the CPU's next VRAM access waits off the bus, so the video transfers stay on time. Since vsd53, MADAM's display-list reads and its memory refresh no longer wait for a copy: while MADAM reads the display list or the picture lines, no copy fill starts, and those reads pass at once when they touch neither the copied row, the written row nor the cache slot being filled.
- `rtl/sdram.sv` gets a block engine for these transfers. The console stops while the engine owns the SDRAM, and the cel engine counts those stopped cycles in its pace (`CE_RP`): a cel takes the time of the timing model, never less.

**How it was checked.** A simulator of the whole system (Verilator, with an SDRAM model that checks the SDRAM timing rules) ran our test discs and Strahl's intro on every version. It showed five defects, all corrected:
- black line pairs in Strahl's animated sequence, before the cache;
- dots on Strahl's title screen, at column 0 of the VRAM rows: the CPU's serial-port cycles were taken as main-bus writes (seen on a MiSTer with vsd43, corrected in vsd44);
- a burst of late words when the screen changes (corrected in vsd46);
- stray dots in the console's audio CD player: a console write seen while the console was stopped in the middle of a memory cycle was lost (corrected in vsd50, checked on a MiSTer);
- the menus of Crime Patrol and Mad Dog McCree jumped by one line and lost colour words: these games copy their background at the top of each frame from rows that are not in the cache, and MADAM's display-list reads waited behind those copies (corrected in vsd53, checked on a MiSTer).

The 24 reference drawings of the test disc give the same CRCs as on the FZ-10. On a real MiSTer, vsd46 ran the 640 cases of the test disc 3.7 with a mean gap of 0.3 % to its simulation.

**Drawing speed.** vsd47 and vsd48 also change the cel engine, from the disc 3.7 measurements of narrow cels:
- 16-bit cels ask for source words when 2 are left in the buffer, not 3;
- refills go before the draw;
- a draw burst stops after 16 pixels for unpacked cels, except solid cels whose source pixel covers more than 1.125 screen pixels, such as fills;
- 16-bit rows without ACE wait 80 ns before the draw restarts.

vsd50 adds a pace of its own for turned or sheared 16-bit cels without ACE, from the measurements of the discs 3.6 and 3.7.

With vsd48, over 898 drawing times of the test discs 3.6 and 3.7, the mean gap to the FZ-10 is 6.8 % (vsd46: 7.6 %), and 80 % are within 10 % (simulation).

**Also in this branch.** The light guns come after the pads in the controller chain, as with a Gamegun plugged into a pad. That is the set-up the Gamegun's documentation asks for Demolition Man. In Demolition Man, pressing the gun's calibration button during the intro freezes the game, on a real FZ-10 too.

Commits on top of `test/kit-20261114i`:
- `12a1806`: light guns after the pads;
- `00ed4dc`: VRAM in the SDRAM module (test core vsd46);
- `76428d8`: narrow-cel timing (vsd47);
- `6638b5a`: no 16-pixel limit for stretched solid cels (vsd48);
- `ee58651`: no game list, the DSP pauses with the console in every game (vsd49);
- `7019f85`: console writes kept when the console stops mid-cycle (vsd50);
- `22f0009`: pace of turned 16-bit cels (vsd50);
- `e6e2d43`: the display list and the refresh no longer wait for a row copy (vsd53).

Later versions come as new commits on top.
