# CD-ROM drive simulation (disc changes)

Verilator testbench for the CD-ROM drive (`rtl/3DO_CDROM.sv`) and the XBUS slave that connects it
(`rtl/3DO/OSA.sv`). It sends the disc info the way Main_MiSTer does and checks:

1. a disc loaded while the console is held in reset (normal game load) gives no media change,
   and the drive reports a disc after the reset (STATUS `0xC1`, boot DIPIR via POLL bit 7);
2. the OS clears Media Access (POLL bit 7) with an XBUS poll write;
3. a disc sent while the console runs (hot swap between discs of the same game) reports
   "door open, no disc" (STATUS `0x01`) during the transfer, then `0xC1`, with exactly one
   CDMDCHG pulse that sets Media Access, so that the OS runs DIPIR on its next bus access;
4. a drive reset through the XBUS reset line gives no media change (a pulse there would make
   every DIPIR, which resets the drive, trigger another one).

`sim_models.sv` replaces `rtl/bram.vhd` (VHDL) and `rtl/hps_ext.v` for Verilator.

    ./run.sh                     # Verilator 5, prints TB PASS or TB FAIL <reason>
    RTL=/path/to/rtl ./run.sh    # simulate another copy of rtl/
