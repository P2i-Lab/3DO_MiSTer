`timescale 1ns/1ps
//============================================================================
//  CD-ROM drive (rtl/3DO_CDROM.sv) + XBUS slave (rtl/3DO/OSA.sv) testbench:
//  block sizes (MODE SET), CD-DA reads, Sub-Q, ABORT/FLUSH and command parsing.
//
//  The host side drives the XBUS like CLIO does for the Portfolio CD-ROM driver
//  (7-byte commands, 1 byte for ABORT); a model of Main_MiSTer answers the READ
//  and NEXT requests the drive sends over the HPS link with sectors.
//  Prints "TB PASS" or "TB FAIL <reason>".
//============================================================================
module tb_cdaudio;

	reg         clk = 0, rst_n = 0, ce = 0;
	// each driven by one process only: m_* by the Main model, t_* by the disc info sender
	reg  [15:0] m_data = '0, t_data = '0;
	reg         m_wr = 0, t_wr = 0, cdd_dten = 0, cdd_dien = 0;
	wire [15:0] cdd_data = cdd_dien ? t_data : m_data;
	wire        cdd_wr = cdd_dien ? t_wr : m_wr;
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
		.CDD_DATA(cdd_data), .CDD_WR(cdd_wr), .CDD_DTEN(cdd_dten), .CDD_DIEN(cdd_dien),
		.EXT_BUS(ext_bus), .TRAY_OPEN(1'b0)
	);

	OSA osa (
		.CLK(clk), .RST_N(rst_n), .EN(1'b1), .CE_R(ce), .CE_F(~ce),
		.EDI(edi), .EDO(edo), .ESTR_N(estr_n), .EWRT_N(ewrt_n), .ECMD_N(ecmd_n), .ESEL_N(esel_n), .ERST_N(erst_n),
		.ERDY_N(erdy_n), .EINT_N(eint_n),
		.CDDI(cd_to_osa), .CDDO(osa_to_cd), .CDEN_N(cden_n), .CDCMD_N(cdcmd_n), .CDHWR_N(cdhwr_n), .CDHRD_N(cdhrd_n),
		.CDRST_N(cdrst_n), .CDSTEN_N(cdsten_n), .CDDTEN_N(cddten_n), .CDMDCHG(cdmdchg),
		.DBG_WAIT_CNT(dbg_wait)
	);

	task automatic wait_clk(input integer n);
		repeat (n) @(posedge clk);
	endtask

	task automatic fail(input string why);
		$display("TB FAIL %s (STATUS=%02x)", why, cd.STATUS);
		$finish;
	endtask

	function automatic integer msf2lba(input [7:0] m, input [7:0] s, input [7:0] f);
		return m * 4500 + s * 75 + f - 150;
	endfunction

	// Sector content sent by the Main model: byte i of LBA lba, then subcode byte i
	function automatic [7:0] pattern(input integer lba, input integer i);
		return 8'((lba * 31) + (i * 7) + ((i >> 8) * 3));
	endfunction
	function automatic [7:0] sub_pattern(input integer lba, input integer i);
		return {1'b0, 7'((lba * 5) + (i * 11))};
	endfunction
	// What the host must read: data_len bytes of data, then sub_len bytes of subcode, then zeros
	function automatic [7:0] expected(input integer lba, input integer i, input integer data_len, input integer sub_len);
		return i < data_len ? pattern(lba, i) : i < data_len + sub_len ? sub_pattern(lba, i - data_len) : 8'h00;
	endfunction

	//------------------------------------------------------------------
	// Main_MiSTer model (support/3do): HPS link commands -> sectors
	//------------------------------------------------------------------
	// Clocked model (a task with delays in a second process did not reach the drive in Verilator)
	integer m_lba = 0, m_left = 0, m_fmt = 0;
	bit     m_req = 0, m_sub = 0;
	bit     m_sub_support = 1;	// 0: Main without subcode (sends only the data)
	reg     m_msb = 0;
	integer m_reads = 0, m_nexts = 0, m_stops = 0, m_sectors = 0;
	integer m_last_fmt = -1;
	integer m_state = 0, m_cnt = 0, m_w = 0, m_words = 0, m_dwords = 0;

	always @(posedge clk) begin
		if (cd.CD_IN_MSB != m_msb) begin
			m_msb <= cd.CD_IN_MSB;
			case (cd.HOST_COMM[0])
				8'h10: begin
					m_lba <= msf2lba(cd.HOST_COMM[1], cd.HOST_COMM[2], cd.HOST_COMM[3]);
					m_left <= {cd.HOST_COMM[4], cd.HOST_COMM[5], cd.HOST_COMM[6]};
					m_fmt <= cd.HOST_COMM[7][1:0];
					m_sub <= cd.HOST_COMM[7][2];
					m_last_fmt <= cd.HOST_COMM[7][2:0];
					m_req <= 1;
					m_reads <= m_reads + 1;
				end
				8'h20: begin
					if (m_left > 0) m_req <= 1;
					m_nexts <= m_nexts + 1;
				end
				default: begin
					m_req <= 0;
					m_stops <= m_stops + 1;
				end
			endcase
		end

		m_wr <= 0;
		case (m_state)
			0: if (m_req && cd.CD_IN_MSB == m_msb) begin	// HPS poll period, then one sector
				m_req <= 0;
				m_cnt <= 300;
				m_state <= 1;
			end
			1: if (m_cnt != 0) m_cnt <= m_cnt - 1; else begin
				cdd_dten <= 1;
				m_w <= 0;
				m_words <= (m_fmt == 1 ? 2352 : m_fmt == 2 ? 2336 : m_fmt == 3 ? 2340 : 2048) / 2;
				m_dwords <= (m_fmt == 1 ? 2352 : m_fmt == 2 ? 2336 : m_fmt == 3 ? 2340 : 2048) / 2;
				if (m_sub && m_sub_support) m_words <= (m_fmt == 1 ? 2352 : m_fmt == 2 ? 2336 : m_fmt == 3 ? 2340 : 2048) / 2 + 48;
				m_cnt <= 4;
				m_state <= 2;
			end
			2: if (m_cnt != 0) m_cnt <= m_cnt - 1; else if (!m_wr) begin
				m_data <= m_w < m_dwords ? {pattern(m_lba, 2*m_w+1), pattern(m_lba, 2*m_w)} :
				          m_w < m_words  ? {sub_pattern(m_lba, 2*(m_w-m_dwords)+1), sub_pattern(m_lba, 2*(m_w-m_dwords))} : 16'h5A5A;	// last word: byte sum
				m_wr <= 1;
				m_w <= m_w + 1;
				if (m_w == m_words) begin m_cnt <= 4; m_state <= 3; end
			end
			3: if (m_cnt != 0) m_cnt <= m_cnt - 1; else begin
				cdd_dten <= 0;
				m_lba <= m_lba + 1;
				m_left <= m_left - 1;
				m_sectors <= m_sectors + 1;
				m_state <= 0;
			end
		endcase
	end

	// Disc info from Main_MiSTer (GetDiscInfo layout): 3 tracks, lead-out 10:00:00
	reg [7:0] toc [2048];
	task automatic send_disc_info();
		for (int i = 0; i < 2048; i++) toc[i] = 8'h00;
		toc[1] = 8'h00; toc[2] = 8'd1; toc[3] = 8'd3; toc[4] = 8'd10; toc[5] = 8'd0; toc[6] = 8'd0;
		// track 1: data 00:02:00, track 2: audio 02:00:00, track 3: audio 05:30:40, then lead-out
		toc[16*1+2] = 8'h04; toc[16*1+3] = 8'd1; toc[16*1+5] = 8'd0;  toc[16*1+6] = 8'd2;  toc[16*1+7] = 8'd0;
		toc[16*2+2] = 8'h00; toc[16*2+3] = 8'd2; toc[16*2+5] = 8'd2;  toc[16*2+6] = 8'd0;  toc[16*2+7] = 8'd0;
		toc[16*3+2] = 8'h00; toc[16*3+3] = 8'd3; toc[16*3+5] = 8'd5;  toc[16*3+6] = 8'd30; toc[16*3+7] = 8'd40;
		toc[16*4+2] = 8'h00; toc[16*4+3] = 8'd4; toc[16*4+5] = 8'd10; toc[16*4+6] = 8'd0;  toc[16*4+7] = 8'd0;
		cdd_dien = 1;
		wait_clk(4);
		for (int w = 0; w < 1024; w++) begin
			t_data = {toc[2*w+1], toc[2*w]};
			t_wr = 1; wait_clk(1);
			t_wr = 0; wait_clk(1);
		end
		wait_clk(4);
		cdd_dien = 0;
		wait_clk(8);
	endtask

	//------------------------------------------------------------------
	// Host side: XBUS cycles as CLIO generates them
	//------------------------------------------------------------------
	task automatic xbus_cycle(input bit sel_n, input bit cmd_n, input bit wrt_n, input [7:0] data, output [7:0] rd);
		{esel_n, ecmd_n, ewrt_n} = {sel_n, cmd_n, wrt_n};
		edi = data;
		wait_clk(2);
		estr_n = 0; wait_clk(4);
		rd = edo;
		estr_n = 1; wait_clk(4);
		{esel_n, ecmd_n, ewrt_n} = 3'b111;
		wait_clk(2);
	endtask

	task automatic select_drive();
		reg [7:0] dummy;
		xbus_cycle(1'b0, 1'b1, 1'b0, 8'h00, dummy);
	endtask

	task automatic send_cmd(input [7:0] c0, input [7:0] c1 = 0, input [7:0] c2 = 0, input [7:0] c3 = 0,
	                        input [7:0] c4 = 0, input [7:0] c5 = 0, input [7:0] c6 = 0);
		reg [7:0] dummy;
		reg [7:0] b [7];
		b = '{c0, c1, c2, c3, c4, c5, c6};
		for (int i = 0; i < (c0 == 8'h08 ? 1 : 7); i++) xbus_cycle(1'b1, 1'b0, 1'b0, b[i], dummy);
	endtask

	reg [7:0] st [32];
	integer   st_len;
	task automatic read_status(input string what);
		integer t;
		t = 0;
		while (cdsten_n) begin
			wait_clk(1);
			t++;
			if (t > 200000) fail({"no status for ", what});
		end
		st_len = 0;
		while (!cdsten_n) begin
			xbus_cycle(1'b1, 1'b0, 1'b1, 8'h00, st[st_len]);
			st_len++;
			if (st_len >= 32) fail({"endless status for ", what});
		end
	endtask

	task automatic expect_status(input string what, input integer len, input [7:0] e0, input [7:0] elast);
		if (st_len != len) begin
			$display("TB INFO %s: got %0d status bytes, first %02x last %02x", what, st_len, st[0], st[st_len-1]);
			fail({what, ": wrong status length"});
		end
		if (st[0] != e0) fail({what, ": wrong status tag"});
		if (st[len-1] != elast) begin
			$display("TB INFO %s: status byte %02x, expected %02x", what, st[len-1], elast);
			fail({what, ": wrong status byte"});
		end
	endtask

	// Reads one block of xfer bytes: data_len bytes of pattern(lba), sub_len bytes of subcode from Main, then zeros
	task automatic read_block(input string what, input integer lba, input integer data_len, input integer xfer, input integer sub_len = 0);
		integer t;
		reg [7:0] b;
		t = 0;
		while (cddten_n) begin
			wait_clk(1);
			t++;
			if (t > 200000) fail({"no data for ", what});
		end
		for (int i = 0; i < xfer; i++) begin
			if (cddten_n) begin
				$display("TB INFO %s: data enable dropped at byte %0d of %0d", what, i, xfer);
				fail({what, ": short block"});
			end
			xbus_cycle(1'b1, 1'b1, 1'b1, 8'h00, b);
			if (b !== expected(lba, i, data_len, sub_len)) begin
				$display("TB INFO %s: lba %0d byte %0d = %02x, expected %02x", what, lba, i, b, expected(lba, i, data_len, sub_len));
				fail({what, ": wrong data"});
			end
		end
		wait_clk(4);
		if (!cddten_n) fail({what, ": data enable still set after the block"});
	endtask

	integer lba0;
	initial begin
		wait_clk(10);
		send_disc_info();
		wait_clk(10);
		rst_n = 1;
		wait_clk(20);
		if (cd.STATUS != 8'hC1) fail("no disc after the load");
		select_drive();

		// Spin up
		send_cmd(8'h02);
		read_status("spin up");
		expect_status("spin up", 2, 8'h02, 8'hE1);

		// 1. Data read, default block size (2048): Main gets format 0
		send_cmd(8'h10, 8'd0, 8'd2, 8'd16, 8'd0, 8'd0, 8'd2);
		lba0 = msf2lba(8'd0, 8'd2, 8'd16);
		read_block("data block 0", lba0, 2048, 2048);
		read_block("data block 1", lba0 + 1, 2048, 2048);
		read_status("data read");
		expect_status("data read", 2, 8'h10, 8'hE1);
		if (m_last_fmt != 0) fail("data read: Main did not get format 0");

		// 2. MODE SET: digital audio, 2352-byte blocks; MODE SENSE gives it back (7 bytes)
		send_cmd(8'h09, 8'h00, 8'h82, 8'h09, 8'h30, 8'h00);
		read_status("mode set DA");
		expect_status("mode set DA", 2, 8'h09, 8'hE1);
		send_cmd(8'h84, 8'h00);
		read_status("mode sense");
		expect_status("mode sense", 7, 8'h84, 8'hE1);
		if (st[1] != 8'h00 || st[2] != 8'h82 || st[3] != 8'h09 || st[4] != 8'h30 || st[5] != 8'h00) fail("mode sense: wrong block parameters");

		// 3. CD-DA read of 3 blocks at 02:00:00 (track 2)
		send_cmd(8'h10, 8'd2, 8'd0, 8'd0, 8'd0, 8'd0, 8'd3);
		lba0 = msf2lba(8'd2, 8'd0, 8'd0);
		read_block("DA block 0", lba0, 2352, 2352);
		read_block("DA block 1", lba0 + 1, 2352, 2352);
		read_block("DA block 2", lba0 + 2, 2352, 2352);
		read_status("DA read");
		expect_status("DA read", 2, 8'h10, 8'hE1);
		if (m_last_fmt != 1) fail("DA read: Main did not get format 1 (2352 bytes)");

		// 4. Sub-Q after it: track 2, index 1, absolute 02:00:03, relative 00:00:03
		send_cmd(8'h87);
		read_status("sub-q");
		expect_status("sub-q", 12, 8'h87, 8'hE1);
		if (st[2] != 8'h01 || st[3] != 8'h02 || st[4] != 8'h01) fail("sub-q: wrong control/track/index");
		if (st[5] != 8'd2 || st[6] != 8'd0 || st[7] != 8'd3) fail("sub-q: wrong absolute time");
		if (st[8] != 8'd0 || st[9] != 8'd0 || st[10] != 8'd3) fail("sub-q: wrong relative time");

		// 5. Sub-Q in track 3 with borrows: one block at 06:10:04, then 06:10:05 - 05:30:40 = 00:39:40
		send_cmd(8'h10, 8'd6, 8'd10, 8'd4, 8'd0, 8'd0, 8'd1);
		read_block("DA block 06:10:04", msf2lba(8'd6, 8'd10, 8'd4), 2352, 2352);
		read_status("DA read 06:10:04");
		send_cmd(8'h87);
		read_status("sub-q 2");
		expect_status("sub-q 2", 12, 8'h87, 8'hE1);
		if (st[3] != 8'h03 || st[4] != 8'h01) fail("sub-q 2: wrong track/index");
		if (st[8] != 8'd0 || st[9] != 8'd39 || st[10] != 8'd40) fail("sub-q 2: wrong relative time");

		// 6. Sub-Q before track 1 (its pregap): index 0, counting down 00:01:00, data track control
		send_cmd(8'h10, 8'd0, 8'd0, 8'd74, 8'd0, 8'd0, 8'd1);
		read_block("DA block 00:00:74", msf2lba(8'd0, 8'd0, 8'd74), 2352, 2352);
		read_status("DA read 00:00:74");
		send_cmd(8'h87);
		read_status("sub-q 3");
		expect_status("sub-q 3", 12, 8'h87, 8'hE1);
		if (st[2] != 8'h41 || st[3] != 8'h01 || st[4] != 8'h00) fail("sub-q 3: wrong control/track/index");
		if (st[8] != 8'd0 || st[9] != 8'd1 || st[10] != 8'd0) fail("sub-q 3: wrong relative time");

		// 7. DA + subcode + error byte (2449 bytes, as the BIOS CD player asks): Main sends the subcode
		send_cmd(8'h09, 8'h00, 8'h82, 8'h09, 8'h30, 8'hC0);
		read_status("mode set DA+subcode");
		send_cmd(8'h10, 8'd5, 8'd30, 8'd50, 8'd0, 8'd0, 8'd2);
		lba0 = msf2lba(8'd5, 8'd30, 8'd50);
		read_block("DA+subcode block 0", lba0, 2352, 2449, 96);
		read_block("DA+subcode block 1", lba0 + 1, 2352, 2449, 96);
		read_status("DA+subcode read");
		expect_status("DA+subcode read", 2, 8'h10, 8'hE1);
		if (m_last_fmt != 5) fail("DA+subcode read: Main not asked for the subcode");

		// 7b. Same with a Main that sends no subcode: the subcode and the error byte read as 0
		m_sub_support = 0;
		send_cmd(8'h10, 8'd5, 8'd31, 8'd0, 8'd0, 8'd0, 8'd1);
		read_block("DA+subcode block, old Main", msf2lba(8'd5, 8'd31, 8'd0), 2352, 2449, 0);
		read_status("DA+subcode read, old Main");
		m_sub_support = 1;

		// 8. ABORT (1 byte) during a 4-block read: no more data, Main told to stop
		send_cmd(8'h09, 8'h00, 8'h82, 8'h09, 8'h30, 8'h00);
		read_status("mode set DA again");
		send_cmd(8'h10, 8'd2, 8'd10, 8'd0, 8'd0, 8'd0, 8'd4);
		lba0 = msf2lba(8'd2, 8'd10, 8'd0);
		read_block("DA block before abort", lba0, 2352, 2352);
		send_cmd(8'h08);
		read_status("abort");
		expect_status("abort", 2, 8'h08, 8'hE1);
		wait_clk(20000);
		if (!cddten_n) fail("abort: data still offered");
		if (m_stops == 0) fail("abort: Main not told to stop");

		// 9. Reads still work after the abort
		send_cmd(8'h10, 8'd2, 8'd20, 8'd0, 8'd0, 8'd0, 8'd1);
		lba0 = msf2lba(8'd2, 8'd20, 8'd0);
		read_block("DA block after abort", lba0, 2352, 2352);
		read_status("read after abort");
		expect_status("read after abort", 2, 8'h10, 8'hE1);

		// 10. Unsupported commands (read header, play): error bit, READ ERROR gives illegal request (0x17)
		send_cmd(8'h86, 8'd0, 8'd2, 8'd0);
		read_status("read header");
		expect_status("read header", 2, 8'h86, 8'hF1);
		send_cmd(8'h82);
		read_status("read error");
		expect_status("read error", 10, 8'h82, 8'hE1);
		if (st[1] != 8'h17 || st[8] != 8'h17) fail("read error: not 'illegal request'");
		send_cmd(8'h0E, 8'd2, 8'd0, 8'd0, 8'hFF, 8'hFF, 8'hFF);
		read_status("play msf");
		expect_status("play msf", 2, 8'h0E, 8'hF1);
		send_cmd(8'h82);
		read_status("read error 2");

		// 11. Seek, no-op: 2-byte status. FLUSH is 7 bytes long: the next command must still be parsed correctly
		send_cmd(8'h01, 8'd0, 8'd2, 8'd0);
		read_status("seek");
		expect_status("seek", 2, 8'h01, 8'hE1);
		send_cmd(8'h05);
		read_status("no-op");
		expect_status("no-op", 2, 8'h05, 8'hE1);
		send_cmd(8'h0B);
		read_status("flush");
		expect_status("flush", 2, 8'h0B, 8'hE1);
		send_cmd(8'h8B);
		read_status("disc info");
		expect_status("disc info", 8, 8'h8B, 8'hE1);
		if (st[2] != 8'd1 || st[3] != 8'd3 || st[4] != 8'd10) fail("disc info: wrong data after flush");

		// 12. Disc code (12 bytes), capacity (7 bytes: the lead-out MSF 6 bytes from the end)
		send_cmd(8'h8A);
		read_status("disc code");
		expect_status("disc code", 12, 8'h8A, 8'hE1);
		send_cmd(8'h85);
		read_status("capacity");
		expect_status("capacity", 7, 8'h85, 8'hE1);
		if (st[7-6] != 8'd10 || st[7-5] != 8'd0 || st[7-4] != 8'd0) fail("capacity: wrong lead-out position");

		// 13. Back to 2048-byte data blocks
		send_cmd(8'h09, 8'h00, 8'h00, 8'h08, 8'h00, 8'h00);
		read_status("mode set data");
		send_cmd(8'h10, 8'd0, 8'd3, 8'd0, 8'd0, 8'd0, 8'd1);
		lba0 = msf2lba(8'd0, 8'd3, 8'd0);
		read_block("data block after DA", lba0, 2048, 2048);
		read_status("data read 2");
		expect_status("data read 2", 2, 8'h10, 8'hE1);
		if (m_last_fmt != 0) fail("data read 2: Main did not get format 0");

		// 14. RESET command: no status, default block parameters
		send_cmd(8'h09, 8'h00, 8'h82, 8'h09, 8'h30, 8'h00);
		read_status("mode set before reset command");
		send_cmd(8'h0A);
		wait_clk(5000);
		if (!cdsten_n) fail("reset command: status sent");
		send_cmd(8'h84, 8'h00);
		read_status("mode sense after reset command");
		expect_status("mode sense after reset command", 7, 8'h84, 8'hE1);
		if (st[2] != 8'h00 || st[3] != 8'h08 || st[4] != 8'h00) fail("reset command: block parameters not restored");

		// 15. Drive reset (XBUS reset line) restores 2048-byte blocks
		send_cmd(8'h09, 8'h00, 8'h82, 8'h09, 8'h30, 8'h00);
		read_status("mode set before reset");
		erst_n = 0; wait_clk(20);
		erst_n = 1; wait_clk(40);
		select_drive();
		send_cmd(8'h10, 8'd0, 8'd3, 8'd10, 8'd0, 8'd0, 8'd1);
		lba0 = msf2lba(8'd0, 8'd3, 8'd10);
		read_block("data block after reset", lba0, 2048, 2048);
		read_status("data read 3");
		if (m_last_fmt != 0) fail("drive reset: block size not restored");
		$display("TB PASS (Main: %0d reads, %0d next, %0d stops, %0d sectors)", m_reads, m_nexts, m_stops, m_sectors);
		$finish;
	end

	initial begin
		#20000000;
		fail("timeout");
	end

endmodule
