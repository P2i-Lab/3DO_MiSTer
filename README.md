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
Input > Light gun: 1 or 2 guns, plugged before the pads as on the console (console - gun 1 - gun 2 - pad).

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
nothing where the picture is dark.

Gun sensor (emulated guns): "Ideal" sees the picture wherever the gun is aimed; "Picture" sees, as the real 3DO
gun, only the bright places of the picture within its field (a disc about 25 lines across around the aim): the gun
reports the first bright place it meets, and a shot at a dark place is not seen.
