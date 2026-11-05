`timescale 1ns/1ps
//============================================================================
//  CD-ROM drive (rtl/3DO_CDROM.sv) + XBUS slave (rtl/3DO/OSA.sv) testbench:
//  disc changes as Main_MiSTer performs them.
//
//  1. Disc info sent while the console is held in reset (normal game load):
//     no media change pulse; the drive reports a disc (STATUS 0xC1) after reset.
//  2. The OS clears Media Access (POLL bit 7) with an XBUS poll write.
//  3. New disc sent while the console runs (hot swap): STATUS goes to 0x01 (door
//     open, no disc) during the transfer then 0xC1, one CDMDCHG pulse, POLL bit 7 set.
//  4. Drive reset through the XBUS reset line: no CDMDCHG pulse.
//  5. Console reset by the watchdog (HOLD_DISC): the disc stays in the drive (STATUS
//     0xC1 during and after the reset), no CDMDCHG pulse.
//  6. Other console resets (OSD reset): the drive forgets the disc until Main sends one.
//  Prints "TB PASS" or "TB FAIL <reason>".
//============================================================================
module tb_cdswap;

	reg         clk = 0, rst_n = 0, ce = 0, hold_disc = 0;
	reg  [15:0] cdd_data = '0;
	reg         cdd_wr = 0, cdd_dien = 0;
	reg  [ 7:0] edi = '0;
	reg         estr_n = 1, ewrt_n = 1, ecmd_n = 1, esel_n = 1, erst_n = 1;
	wire [35:0] ext_bus;

	wire [ 7:0] cd_to_osa, osa_to_cd, edo;
	wire        cden_n, cdcmd_n, cdhwr_n, cdhrd_n, cdrst_n, cdsten_n, cddten_n, cdmdchg;
	wire        erdy_n, eint_n;
	wire [12:0] dbg_wait;

	always #1 clk = ~clk;
	always @(posedge clk) ce <= ~ce;

	P3DO_CDROM cd (
		.CLK(clk), .RST_N(rst_n), .EN(1'b1), .CE(ce),
		.CDDI(osa_to_cd), .CDDO(cd_to_osa),
		.CDEN_N(cden_n), .CDCMD_N(cdcmd_n), .CDHWR_N(cdhwr_n), .CDHRD_N(cdhrd_n), .CDRST_N(cdrst_n),
		.CDSTEN_N(cdsten_n), .CDDTEN_N(cddten_n), .CDMDCHG(cdmdchg),
		.CDD_DATA(cdd_data), .CDD_WR(cdd_wr), .CDD_DTEN(1'b0), .CDD_DIEN(cdd_dien),
		.EXT_BUS(ext_bus), .TRAY_OPEN(1'b0), .HOLD_DISC(hold_disc)
	);

	OSA osa (
		.CLK(clk), .RST_N(rst_n), .EN(1'b1), .CE_R(ce), .CE_F(~ce),
		.EDI(edi), .EDO(edo), .ESTR_N(estr_n), .EWRT_N(ewrt_n), .ECMD_N(ecmd_n), .ESEL_N(esel_n), .ERST_N(erst_n),
		.ERDY_N(erdy_n), .EINT_N(eint_n),
		.CDDI(cd_to_osa), .CDDO(osa_to_cd), .CDEN_N(cden_n), .CDCMD_N(cdcmd_n), .CDHWR_N(cdhwr_n), .CDHRD_N(cdhrd_n),
		.CDRST_N(cdrst_n), .CDSTEN_N(cdsten_n), .CDDTEN_N(cddten_n), .CDMDCHG(cdmdchg),
		.DBG_WAIT_CNT(dbg_wait)
	);

	// Written only here (the initial block only reads them)
	integer pulses = 0;
	integer door_open_cycles = 0;
	reg [7:0] last_status = 8'hXX;
	reg       last_loaded = 0;
	always @(posedge clk) begin
		if (cdmdchg) pulses <= pulses + 1;
		if (cd.STATUS == 8'h01) door_open_cycles <= door_open_cycles + 1;
		last_status <= cd.STATUS;
		last_loaded <= cd.CDD_IS_LOADED;
		if (cd.STATUS != last_status || cd.CDD_IS_LOADED != last_loaded)
			$display("TB TRACE t=%0t rst_n=%b loaded=%b status=%02x cdrst_n=%b", $time, rst_n, cd.CDD_IS_LOADED, cd.STATUS, cdrst_n);
	end

	task automatic wait_clk(input integer n);
		repeat (n) @(posedge clk);
	endtask

	// Disc info transfer from Main_MiSTer: 1024 16-bit words on the CD info channel
	task automatic send_disc_info();
		cdd_dien = 1;
		wait_clk(4);
		for (int i = 0; i < 1024; i++) begin
			cdd_data = i[15:0];
			cdd_wr = 1; wait_clk(1);
			cdd_wr = 0; wait_clk(1);
		end
		wait_clk(4);
		cdd_dien = 0;
		wait_clk(8);
	endtask

	// One XBUS strobe as CLIO generates it ({sel_n,cmd_n,wrt_n} select the cycle type)
	task automatic xbus_cycle(input bit sel_n, input bit cmd_n, input bit wrt_n, input [7:0] data);
		{esel_n, ecmd_n, ewrt_n} = {sel_n, cmd_n, wrt_n};
		edi = data;
		wait_clk(2);
		estr_n = 0; wait_clk(4);
		estr_n = 1; wait_clk(4);
		{esel_n, ecmd_n, ewrt_n} = 3'b111;
		wait_clk(2);
	endtask

	task automatic clear_media_access();
		xbus_cycle(1'b0, 1'b1, 1'b0, 8'h00);	// select device 0 (the CD drive)
		xbus_cycle(1'b0, 1'b0, 1'b0, 8'h80);	// poll write: bit 7 = clear Media Access
	endtask

	task automatic fail(input string why);
		$display("TB FAIL %s (STATUS=%02x POLL=%02x pulses=%0d)", why, cd.STATUS, osa.POLL, pulses);
		$finish;
	endtask

	integer door_open_before;
	initial begin
		// 1. Normal load: the disc info arrives while the console is in reset
		wait_clk(10);
		send_disc_info();
		wait_clk(10);
		rst_n = 1;
		wait_clk(20);
		if (pulses != 0) fail("media change pulse on a load during reset");
		if (cd.STATUS != 8'hC1) fail("no disc reported after the initial load");
		if (!osa.POLL[7]) fail("Media Access not set after reset (boot DIPIR)");

		// 2. The OS clears Media Access
		clear_media_access();
		if (osa.POLL[7]) fail("Media Access not cleared by the poll write");

		// 3. Hot swap: new disc info while the console runs
		door_open_before = door_open_cycles;
		send_disc_info();
		wait_clk(20);
		if (door_open_cycles == door_open_before) fail("no door-open status during the swap");
		if (cd.STATUS != 8'hC1) fail("no disc reported after the swap");
		if (pulses != 1) fail("expected exactly one media change pulse for the swap");
		if (!osa.POLL[7]) fail("Media Access not set after the swap");

		// 4. Drive reset through the XBUS: no media change
		clear_media_access();
		erst_n = 0; wait_clk(20);
		erst_n = 1; wait_clk(40);
		if (pulses != 1) fail("media change pulse on a drive reset");
		if (cd.STATUS != 8'hC1) fail("no disc reported after a drive reset");

		// 5. Watchdog reset of the console: the disc stays in
		hold_disc = 1;
		rst_n = 0; wait_clk(20);
		if (!cd.CDD_IS_LOADED) fail("disc dropped by a watchdog reset");
		if (cd.STATUS != 8'hC1) fail("no disc reported during a watchdog reset");
		rst_n = 1; wait_clk(4);
		hold_disc = 0; wait_clk(40);
		if (pulses != 1) fail("media change pulse on a watchdog reset");
		if (cd.STATUS != 8'hC1) fail("no disc reported after a watchdog reset");

		// 6. Other console reset: the disc is forgotten (Main sends it again on a load)
		rst_n = 0; wait_clk(20);
		rst_n = 1; wait_clk(40);
		if (cd.CDD_IS_LOADED || cd.STATUS != 8'h81) fail("disc kept by a reset without HOLD_DISC");
		if (pulses != 1) fail("media change pulse on a console reset");

		$display("TB PASS (pulses=%0d)", pulses);
		$finish;
	end

	initial begin
		#200000;
		fail("timeout");
	end

endmodule
