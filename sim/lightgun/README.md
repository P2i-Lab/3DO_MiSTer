# Light gun simulation

Verilator testbench for `rtl/lightgun.sv`: the 3DO light gun (control port device `0x4D`), the
crosshairs drawn over the picture and the reader of PlayStation GunCons on the SNAC port.

The record of the gun was measured on a Japanese FZ-10 with two real guns (bench disc 3.4):

| Byte | Content |
|---|---|
| 0 | `4D` |
| 1 | buttons: `80` trigger, `48` calibration button (both: `C8`); bit 0 = counter bit 19 |
| 2-3 | counter bits 18-3 |
| 4 | counter bits 2-0, then the number of lines the gun saw (5 bits) |

The counter counts at about 20 MHz from the start of the field to the first line where the gun sees
the beam: 1270.4 per line and 3.19 per 3DO pixel along a line, measured on an FZ-10 with a real gun
at rest (bench page PISTOLET BALAYAGE); it is always even. For an aim at line y and dot d (two dots
per 3DO pixel) the emulated gun answers 1270.4 x (y + 11) + 236.1 + 1.595 x d: on the middle line
(120) this is the counter of the earlier fit on aimed shots, 1271.1 x (y + 11) + 144 + 1.595 x d.
When the gun sees nothing, the counter and the line count are 0 (this is how the games reload:
trigger pressed and nothing seen).

The testbench checks the record for joystick aims (the 9 crosses of the bench page PISTOLET
CIBLES, the centre, the corners), the off-picture values of the stick, the buttons (trigger,
reload, calibration), the PAL counter, the mouse aim (moves, margins, buttons), the crosshairs
over a 640x240 picture for each size, with one or two guns, a gun off the picture and no gun;
then the picture sensor (first lit place in the disc of the gun, lines lit, the record it gives) on a black picture with a white block; then, with a model of two GunCons on the SNAC pins (PlayStation pad protocol, ACK after each
byte), the request bytes, the aim and buttons read on both ports, the 3DO gun record it gives,
the edges of the picture, a GunCon that sees nothing and unplugged GunCons. It writes the
picture with the small crosshairs to `cross.png` in the build directory.

    ./run.sh                     # Verilator 5, prints TB PASS or TB FAIL
    RTL=/path/to/rtl ./run.sh    # simulate another copy of rtl/
