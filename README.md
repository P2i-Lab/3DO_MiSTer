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

## Gamepads and light guns
Input > Gamepads: 1 to 4 gamepads, or None. Input > Light gun: Off, Mouse, USB gun, 2 USB guns, USB gun+mouse,
SNAC GunCon, 2 SNAC GunCons or SNAC Saturn gun. Lines that cannot be used are hidden. Gun settings made before
vsd57 are not kept: choose the gun again.

The gamepads take MiSTer's players 1, 2... in order, and a USB gun the player after them: with 1 gamepad, a USB
gun is player 2; with 2 gamepads, player 3; a second USB gun is the next player. The mouse is never a player.
MiSTer gives the player numbers in the order the controllers are first pressed: press the gamepad first, then the
gun. If the order is wrong, go back from the core's menu, press Right for the System page and pick "Reset player
assignment"; the help lines of the Input page say so. In the 3DO controller chain the guns come after the pads, as
a Gamegun plugged into a pad (console - pad - gun 1 - gun 2); with no pad the guns come first.

| Gun | USB gun | Mouse | GunCon | Saturn gun |
|---|---|---|---|---|
| Trigger | A | left button | trigger | trigger |
| Reload (shot off the screen) | B | right button | B | shot off the screen |
| Calibration button | C | middle button | A | Start |

A USB gun is a light gun that MiSTer sees as a joystick, or a stick: it aims with its left stick. Aiming off the
picture (the extreme values of the stick) is also a shot off the screen. The flightstick and the 3DO mouse are off
while guns are used. Crosshairs: gun 1 red, gun 2 green (Input > Crosshair).

With "SNAC GunCon" or "2 SNAC GunCons", the guns are Namco GunCons (PlayStation) on a PlayStation SNAC adapter
(port 1: gun 1, port 2: gun 2), aimed at a CRT fed by the analog video output (15 kHz). The core sends the
composite sync to the GunCon on pin 6 of the adapter, as the PlayStation core does. As with the real 3DO gun, a
GunCon sees nothing where the picture is dark. GunCon X adjust moves the aim of both GunCons left (negative) or
right (positive) by 2 pixels a step, from -32 to +30 pixels. 0 is the place measured with a real GunCon on a CRT
(the crosshair on the aim at the centre and at both edges); another GunCon or screen may want a step or two: aim
at a precise point of a still bright picture (the bullseye of a game's calibration screen, say) and pick the value
that puts the crosshair on it. The 3DO gun reports the adjusted aim, so games see the same shift (their own
calibration takes care of any offset).

With "SNAC Saturn gun", the gun is a Sega Saturn light gun on a Saturn SNAC adapter, which has one port: one gun.
The Saturn gun gives no position: its cell sees the beam of the CRT pass, the core then notes where its own scan
is, and gives the game the values of a 3DO gun aimed at the same place. So it needs a CRT on the analog video
output, as the GunCon does. Saturn X adjust moves the aim by 2 pixels a step, from -32 to +30, for the time the
gun takes to react: set Crosshair to Small, aim at a precise point, and change the value until the crosshair
falls where you aim; then set Crosshair back to None to play, since the gun also sees the crosshair. Not yet tried
with a real Saturn gun: it was simulated from its description.

Gun sensor (Mouse and USB guns): "Ideal" sees the picture wherever the gun is aimed; "Picture" sees, as the real
3DO gun, only the bright places of the picture within its field (a disc about 25 lines across around the aim): the
gun reports the first bright place it meets, and a shot at a dark place is not seen.

Input > Show controllers: a box at the top left of the picture lists each player number and what the controller
that got it does (for example "Player 1 Gamepad 1", "Player 2 Light gun 1", "Player 3 Not used"), then the mouse,
and the SNAC port when a SNAC gun is chosen. While a button of a controller is pressed, its line turns yellow, with
a star: it shows which number MiSTer gave to each controller, and where an extra one went.

## P2i Lab fork

This branch of P2i Lab's fork of [srg320's 3DO core](https://github.com/MiSTer-devel/3DO_MiSTer) holds the SDRAM core. The official core is srg320's: this is a test core. The fork's [main README](https://github.com/P2i-Lab/3DO_MiSTer#p2i-lab-fork-work-in-progress) lists all our changes.

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

**Also in this branch.** The light guns come after the pads in the controller chain, as with a Gamegun plugged into a pad. That is the set-up the Gamegun's documentation asks for Demolition Man. In Demolition Man, pressing the gun's calibration button during the intro freezes the game, on a real FZ-10 too. Since vsd57, the Input page is simpler, a box can show which player each controller is, and a Sega Saturn gun works on the SNAC port: see [Gamepads and light guns](#gamepads-and-light-guns) above.

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
