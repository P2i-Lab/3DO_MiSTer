# CD audio simulation

Tests for audio CD (CD-DA) support: the drive in the core, and the sector source in Main_MiSTer.

## Core: `run.sh`

Verilator testbench for the CD-ROM drive (`rtl/3DO_CDROM.sv`) behind the XBUS slave
(`rtl/3DO/OSA.sv`). The host side drives the XBUS like CLIO does for the Portfolio CD-ROM
driver: 7-byte commands (1 byte for ABORT), then status and data reads. A clocked model of
Main_MiSTer answers the READ and NEXT requests the drive sends over the HPS link with sectors.

It checks:

1. 2048-byte data reads (the default block size), so data discs work as before;
2. MODE SET page 0 (density 0x82, 2352-byte blocks) and MODE SENSE (7 bytes);
3. CD-DA reads: Main is asked for 2352-byte raw blocks and the host gets them unchanged;
4. READ SUB-Q (0x87, 12 bytes): track, index, absolute and relative time, borrows, the pregap
   of track 1 (index 0, counting down);
5. 2449-byte blocks (CD-DA + subcode + error byte, as the BIOS CD player reads them): the
   96 subcode bytes come from Main, the error byte reads 0; with a Main that sends no subcode,
   the subcode reads 0;
6. ABORT during a read: no more data, Main told to stop, reads work again afterwards;
7. unsupported commands (read header, play) return the error bit, and READ ERROR gives
   "illegal request" (0x17); SEEK and NO-OP return 2 bytes;
8. FLUSH is 7 bytes long: the next command is still parsed correctly;
9. disc code (12 bytes) and capacity (7 bytes: the OS reads the lead-out 6 bytes from the end);
10. the RESET command returns no status and restores 2048-byte blocks, as does the XBUS reset line.

`sim_models.sv` replaces `rtl/bram.vhd` (VHDL) and `rtl/hps_ext.v` for Verilator.

    ./run.sh                     # Verilator 5, prints TB PASS or TB FAIL <reason>
    RTL=/path/to/rtl ./run.sh    # simulate another copy of rtl/ (upstream fails at MODE SENSE)

## Main_MiSTer: `main/run.sh`

Builds `support/3do/3docdd.cpp` from a Main_MiSTer checkout with stubbed file and CHD access,
writes CUE/BIN and ISO images and checks every byte Main sends to the core: the TOC (control
bytes 0x14/0x10, CUE FLAGS), CD-DA samples in big-endian order (BIN swapped, MOTOROLA not),
track boundaries, raw data sectors (2352, 2340, and sync + header for ISO images), and the
Q subcode (positions in BCD, CRC).

    MAIN=/path/to/Main_MiSTer main/run.sh    # prints MAIN TEST PASS or MAIN TEST FAIL
