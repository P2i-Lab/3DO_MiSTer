# NVRAM save file simulation

Verilator testbench for the loading and saving of the NVRAM save file. `prepare.py` copies the
save/load logic of `3DO.sv` (the "STATE SAVE/LOAD" section up to the `P3DO_NVRAM_FMT` instance,
and the `nvram_*` connections of the `ddram` instance) into the testbench, which adds models of
the HPS link (Main_MiSTer answering the sector requests of the core through `sys/hps_io.sv`) and
of the NVRAM port of `rtl/ddram.sv`. For each test save file, the testbench loads it (mounted by
Main_MiSTer or loaded from the OSD), checks the 16384 words of the NVRAM, saves it back from the
OSD and checks the file written.

| Save file | Expected |
|---|---|
| `main_blank`: the blank save Main_MiSTer gives a new game | loaded unchanged |
| `zero`, `zero_again`: 32 KB of zeros (or an empty file) | Main_MiSTer's blank save |
| `ff`: 32 KB of `0xFF` | Main_MiSTer's blank save |
| `garbage`: random bytes | Main_MiSTer's blank save |
| `bad_sync`: blank volume with a wrong sync byte, random data after sector 0 | Main_MiSTer's blank save |
| `version1`: label with structure version 1, random data | Main_MiSTer's blank save |
| `in_use`: volume with label flags and data in every sector | loaded unchanged |

With the `3DO.sv` of the original core (no `P3DO_NVRAM_FMT`), the six files without a valid
volume label fail: they are loaded as they are.

`sim_models.sv` replaces `rtl/bram.vhd` (VHDL) for Verilator.

    ./run.sh                               # Verilator 5 and python3, prints TB PASS or TB FAIL
    CORE=/path/to/core ./run.sh            # another copy of the core (3DO.sv and rtl/)
    MAIN=/path/to/Main_MiSTer ./run.sh     # also check the blank save against Main_MiSTer's
