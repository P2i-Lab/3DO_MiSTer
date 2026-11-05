# Pause meter simulation

Verilator testbench for `rtl/pause_meter.sv`, the "Pause meter" debug option. While the console
waits for the DDR3 (the part of the VRAM outside the FPGA memory, the BIOS ROM, the NVRAM), the
CPU and MADAM are paused while the video keeps running. The meter draws, for each frame, the
share of time spent paused:

| Bar | Measure |
|---|---|
| 1, red | all pauses |
| 2, yellow | VRAM in DDR3: random access not in the cache (`ddr_busy`) |
| 3, cyan | VRAM page transfer (SPORT) still running (`VRAM_LRDY`/`VRAM_RRDY` low) |
| 4, green | BIOS ROM or NVRAM access in DDR3 (`ddr_io_busy`) |
| 5, magenta | longest single pause of the frame, 1 pixel per microsecond |

Bars 1 to 4 are 256 pixels for 100 %, with grey marks every 25 %; the white mark is the highest
value of the last 64 frames. A pause can have several causes at once, so the bars 2 to 4 can add
up to more than bar 1.

Bar 5 tests a cause of crackling sound: during a pause the DMA that feeds the DSP input FIFOs
stops, while the DSP keeps playing at 44.1 kHz. A FIFO holds 8 samples, 181 us (light mark), or
about half of that if it was not full when the pause began (mark at 90 us). Pauses that reach
these marks while the sound crackles would point to FIFO underruns.

The testbench drives a 288x64 picture and a fixed pause pattern (25 % paused: 10 % VRAM in DDR3,
10 % page transfers, 5 % ROM/NVRAM), checks the bar lengths (64, 25, 25 and 12 pixels, and 0 for bar 5 since these pauses last 0.5 us),
that nothing else in the picture changes; then adds one 6000-cycle (120 us) pause per frame and
checks that bar 5 is 120 pixels; then that the picture is unchanged with the meter off. It writes
the picture with the long pauses to `meter.png` in the build directory.

    ./run.sh                     # Verilator 5, prints TB PASS or TB FAIL
    RTL=/path/to/rtl ./run.sh    # simulate another copy of rtl/
