# Virtual MiSTer (whole core simulation)

`run.sh` builds the whole 3DO core (`3DO.sv` and `rtl/`) with Verilator, around:

- models of the MiSTer board: SDRAM chip, DDR3 reached through the HPS (Avalon, `+ddr_lat=N`
  clk_mem cycles of read latency, 16 by default; `+ddr_jitter=N` adds 0..N cycles to each read,
  `+ddr_spike_pm=P +ddr_spike=L` makes P reads per thousand wait L more cycles, as when the HPS
  and the video scaler share the DDR3), clocks (`sim_top.sv`);
- models of the Intel blocks and framework modules the core uses (`sim_models.sv`): MLAB and
  M10K RAMs, divider, PLLs, video output passed through without scaler;
- the HPS link (`hps_io_sim.sv`) driven by `sim_main.cpp`, which runs the 3DO code of a
  Main_MiSTer clone (`support/3do`, compiled unchanged; file access, SPI and timers in
  `main_stubs`): BIOS and kanji ROM loading, CD drive commands and sectors, save file.

The pictures are written as PNG, the sound as WAV; controller inputs and disc swaps are
scripted by frame number.

```
MAIN_REPO=/path/to/Main_MiSTer MAIN_REF=branch ./run.sh \
    --bios panafz1.bin --cd "Game (USA).cue" --frames 900 --png every:60 --wav --out /tmp/out \
    --press 600-610:A --swap 800:"Game (USA) (Disc 2).cue"
```

`MAIN_DIR=folder` takes `support/3do` from a working folder instead of `MAIN_REF`, `CORE=folder`
simulates another copy of the core, `BUILD=folder` keeps several builds side by side.

Debugging aids (options of the simulator):

- `--progress N`: every N frames, the picture's lit area, the CPU pause time and the longest
  pause, the range of CPU fetch addresses, and the DSP activity;
- `--watch LO-HI` (several allowed), `--watch-from F`, `--watch-writes`, `--watch-max N`: CPU
  reads and writes in an address range, with the program counter;
- `--pc-hist F1-F2`: the 40 busiest 16-byte blocks of CPU fetches during frames F1..F2 (where
  the CPU loops or waits);
- `+cd_trace`: the commands of the OS to the CD drive, the drive replies and the requests to
  Main; `--cd-log N`: the sectors Main sends;
- `+dsp_trace=F`: from frame F, once per frame, the DSP input FIFOs, DMA enables, interrupts;
- `+ioread=F1 +ioread_end=F2`: CPU reads of CLIO registers (DSP output memory, timers, XBUS)
  in frames F1..F2 as CLIO sees them, with the value returned (`--watch` misses the reads the
  ARM announces before an internal cycle); `+ioread_lo=H +ioread_hi=H` keep a range of
  registers (hex offsets). XBUS registers (0x400 and up) answer later: the value printed for
  them is the one of the previous XBUS read;
- `+madread=F1 +madread_end=F2`: CPU reads of MADAM registers (DMA stack, cel engine) in frames
  F1..F2, with the value returned;
- `+fifo_stats`: every 60 frames, the DSP reads of each input FIFO and the ones that found it
  empty (underruns, heard as clicks), with the longest system pause.

Test discs and sound checks (Python 3, no extra module):

- `make_audio_cd.py`: 3 tracks of steady tones, a different frequency on each channel
  (`wav_tones.py` tells which tone each half second of a recording holds);
- `make_steps_cd.py`: a tone stepping up every half second (`wav_steps.py` reads the steps back
  and flags skips and wrong speeds, `--swap` when the channels come the other way round); `--be`
  writes big-endian samples for emulators that swap BIN audio;
- `wav_compare.py`: aligns two recordings of the same moment (e.g. the simulation and an
  emulator) and correlates their channels, to compare loudness and stereo orientation.

What the simulation does not reproduce:

- the video clock: 50 MHz instead of 49.09 MHz, so frames are 1.8% shorter than on the console;
- the exact DDR3 latency of the board (see `+ddr_jitter` above), so pause lengths are indicative;
- the scaler, HDMI and analog outputs of the framework, the OSD;
- the FPGA timing (only the Quartus report tells whether the build meets it);
- CHD images (convert them with `chdman extractcd`).

Source changes for Verilator, made on the build copy only (`run.sh`):

- the code between `synopsys translate_off` and `translate_on` is removed, as Quartus does
  (the `` `define SIM `` and `DEBUG` lines and debug code there);
- variables declared with an initial value inside an always block get it from an initial block
  (`fix_inits.py`): Verilator 5.0 otherwise reassigns it every time the block runs;
- `wire X[N] = '{...}` is written as a declaration and an assign (not parsed by Verilator 5.020);
- the SDRAM data bus of `rtl/sdram.sv` is written as a registered output enable and data
  (Verilator does not support `<= 'Z` in a clocked block).
