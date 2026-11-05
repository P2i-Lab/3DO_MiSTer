// Virtual MiSTer board: the core (emu, 3DO.sv) with models of the SDRAM chip and of the DDR3
// reached through the HPS. Clocks and the Linux side (Main_MiSTer) come from sim_main.cpp.

module sim_top
(
	input         clk_sys,     // 50 MHz: CLK_50M, clk_sys and the video clock
	input         clk_mem,     // 100 MHz, rises together with clk_sys
	input         reset,

	output        CE_PIXEL,
	output  [7:0] VGA_R,
	output  [7:0] VGA_G,
	output  [7:0] VGA_B,
	output        VGA_HS,
	output        VGA_VS,
	output        VGA_DE,
	output [15:0] AUDIO_L,
	output [15:0] AUDIO_R,
	output        CPU_PAUSE,   // the console waits for the DDR3 (pause of 3DO.sv)

	output [31:0] DBG_CPU_A,   // ARM60 address
	output [31:0] DBG_CPU_DO,  // ARM60 write data
	output [31:0] DBG_CPU_DI,  // ARM60 read data
	output        DBG_FETCH,   // opcode fetch at DBG_CPU_A completes in this cycle
	output        DBG_WRITE,   // data write at DBG_CPU_A completes in this cycle
	output        DBG_READ,    // data read at DBG_CPU_A completes in this cycle

	output        DBG_DSP_GW,      // the DSP runs
	output  [9:0] DBG_DSP_PC,
	output        DBG_DMA_TO_DSP,  // a DMA channel writes into a DSP input FIFO
	output [12:0] DBG_EIFIFO_NE    // DSP input FIFOs that hold samples
);
	assign DBG_DSP_GW = emu.p3do.clio.DSP_GW;
	assign DBG_DSP_PC = emu.p3do.clio.DSP_PC;
	assign DBG_DMA_TO_DSP = emu.p3do.clio.DMA_TO_DSP_WRITE[0] | emu.p3do.clio.DMA_TO_DSP_WRITE[1];
	genvar fi;
	generate for (fi = 0; fi < 13; fi++) begin : fifo_ne
		assign DBG_EIFIFO_NE[fi] = |emu.p3do.clio.EIFIFO_COUNT[fi];
	end endgenerate
	wire cpu_cycle = emu.p3do.MCLK_PH2 & ~emu.p3do.CPU_nMREQ & ~emu.pause;
	assign DBG_CPU_A  = emu.p3do.CPU_A;
	assign DBG_CPU_DO = emu.p3do.CPU_DO;
	assign DBG_CPU_DI = emu.p3do.CPU_DI;
	assign DBG_FETCH  = cpu_cycle & ~emu.p3do.CPU_nOPC;
	assign DBG_WRITE  = cpu_cycle &  emu.p3do.CPU_nRW;
	assign DBG_READ   = cpu_cycle & ~emu.p3do.CPU_nRW & emu.p3do.CPU_nOPC;
	wire [45:0] HPS_BUS;
	wire  [3:0] ADC_BUS;

	wire        SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nCS, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nWE;
	wire [12:0] SDRAM_A;
	wire  [1:0] SDRAM_BA;
	wire [15:0] SDRAM_DQ;

	wire        DDRAM_CLK, DDRAM_BUSY, DDRAM_DOUT_READY, DDRAM_RD, DDRAM_WE;
	wire  [7:0] DDRAM_BURSTCNT, DDRAM_BE;
	wire [28:0] DDRAM_ADDR;
	wire [63:0] DDRAM_DOUT, DDRAM_DIN;

	emu emu
	(
		.CLK_50M(clk_sys),
		.RESET(reset),
		.HPS_BUS(HPS_BUS),
		.CLK_VIDEO(),
		.CE_PIXEL(CE_PIXEL),
		.VIDEO_ARX(),
		.VIDEO_ARY(),
		.VGA_R(VGA_R),
		.VGA_G(VGA_G),
		.VGA_B(VGA_B),
		.VGA_HS(VGA_HS),
		.VGA_VS(VGA_VS),
		.VGA_DE(VGA_DE),
		.VGA_F1(),
		.VGA_SL(),
		.VGA_SCALER(),
		.VGA_DISABLE(),
		.HDMI_WIDTH(12'd0),
		.HDMI_HEIGHT(12'd0),
		.HDMI_FREEZE(),
		.HDMI_BLACKOUT(),
		.HDMI_BOB_DEINT(),
		.LFB_FMT(6'd0),
		.LFB_BASE(32'd0),
		.LFB_WIDTH(12'd0),
		.LFB_HEIGHT(12'd0),
		.LFB_STRIDE(14'd0),
		.LED_USER(),
		.LED_POWER(),
		.LED_DISK(),
		.BUTTONS(),
		.CLK_AUDIO(1'b0),
		.AUDIO_L(AUDIO_L),
		.AUDIO_R(AUDIO_R),
		.AUDIO_S(),
		.AUDIO_MIX(),
		.ADC_BUS(ADC_BUS),
		.SD_SCK(),
		.SD_MOSI(),
		.SD_MISO(1'b0),
		.SD_CS(),
		.SD_CD(1'b0),
		.DDRAM_CLK(DDRAM_CLK),
		.DDRAM_BUSY(DDRAM_BUSY),
		.DDRAM_BURSTCNT(DDRAM_BURSTCNT),
		.DDRAM_ADDR(DDRAM_ADDR),
		.DDRAM_DOUT(DDRAM_DOUT),
		.DDRAM_DOUT_READY(DDRAM_DOUT_READY),
		.DDRAM_RD(DDRAM_RD),
		.DDRAM_DIN(DDRAM_DIN),
		.DDRAM_BE(DDRAM_BE),
		.DDRAM_WE(DDRAM_WE),
		.SDRAM_CLK(SDRAM_CLK),
		.SDRAM_CKE(SDRAM_CKE),
		.SDRAM_A(SDRAM_A),
		.SDRAM_BA(SDRAM_BA),
		.SDRAM_DQ(SDRAM_DQ),
		.SDRAM_DQML(SDRAM_DQML),
		.SDRAM_DQMH(SDRAM_DQMH),
		.SDRAM_nCS(SDRAM_nCS),
		.SDRAM_nCAS(SDRAM_nCAS),
		.SDRAM_nRAS(SDRAM_nRAS),
		.SDRAM_nWE(SDRAM_nWE),
		.UART_CTS(1'b0),
		.UART_RTS(),
		.UART_RXD(1'b0),
		.UART_TXD(),
		.UART_DTR(),
		.UART_DSR(1'b0),
		.USER_IN(7'h7F),
		.USER_OUT(),
		.OSD_STATUS(1'b0)
	);

	assign CPU_PAUSE = emu.pause;

	// +dsp_trace=N: from frame N, once per frame, the DSP input FIFOs (count, writes and reads
	// during the frame), the DMA channels enabled for them and their pending refills
	int dsp_trace_from = -1, vframe = 0;
	initial if (!$value$plusargs("dsp_trace=%d", dsp_trace_from)) dsp_trace_from = -1;
	int fwr [13], frd [13];
	int xb_sten = 0, xb_dten = 0, xb_reads = 0, xb_cyc = 0;
	bit vbl_old, hrd_old;
`define FIFO_COUNT(n) \
	if (emu.p3do.clio.EIFIFO``n``.EN && emu.p3do.clio.EIFIFO``n``.WRREQ) fwr[n]++; \
	if (emu.p3do.clio.EIFIFO``n``.EN && emu.p3do.clio.EIFIFO``n``.RDREQ) frd[n]++;
	always @(posedge clk_sys) begin
		`FIFO_COUNT(0) `FIFO_COUNT(1) `FIFO_COUNT(2) `FIFO_COUNT(3) `FIFO_COUNT(4) `FIFO_COUNT(5) `FIFO_COUNT(6)
		`FIFO_COUNT(7) `FIFO_COUNT(8) `FIFO_COUNT(9) `FIFO_COUNT(10) `FIFO_COUNT(11) `FIFO_COUNT(12)
		xb_cyc++;
		if (!emu.CDSTEN_N) xb_sten++;
		if (!emu.CDDTEN_N) xb_dten++;
		if (!emu.CDHRD_N && hrd_old) xb_reads++;
		hrd_old <= emu.CDHRD_N;
		vbl_old <= emu.VBL_N;
		if (vbl_old && !emu.VBL_N) begin
			if (dsp_trace_from >= 0 && vframe >= dsp_trace_from) begin
				$write("[dsp f%0d] gw %0d dmaen %08X pend %04X int0 %08X/%08X int1 %08X/%08X | drive status %0d%% data %0d%% host reads %0d |",
				       vframe, emu.p3do.clio.DSP_GW, emu.p3do.clio.DMAEN, emu.p3do.clio.DMA_TO_DSP_PEND,
				       emu.p3do.clio.INT0_PEND, emu.p3do.clio.INT0_EN, emu.p3do.clio.INT1_PEND, emu.p3do.clio.INT1_EN,
				       100 * xb_sten / xb_cyc, 100 * xb_dten / xb_cyc, xb_reads);
				for (int i = 0; i < 13; i++)
					if (fwr[i] || frd[i] || emu.p3do.clio.EIFIFO_COUNT[i])
						$write(" f%0d:%0d w%0d r%0d", i, emu.p3do.clio.EIFIFO_COUNT[i], fwr[i], frd[i]);
				$write("\n");
			end
			vframe++;
			for (int i = 0; i < 13; i++) begin fwr[i] = 0; frd[i] = 0; end
			xb_sten = 0; xb_dten = 0; xb_reads = 0; xb_cyc = 0;
		end
	end

	// +fifo_stats: DSP reads of the input FIFOs (bumps) and the ones that found the FIFO empty
	// (underruns: the DSP gets a stale sample, heard as a click), printed every 60 frames per channel,
	// with the longest system pause of the period; totals at the end
	bit fifo_stats = 0;
	initial fifo_stats = $test$plusargs("fifo_stats");
	int fpop [13], fempty [13], tpop [13], tempty [13];
	int pause_run = 0, pause_max = 0;
`define FIFO_UNDER(n) \
	if (emu.p3do.clio.EIFIFO``n``.EN && emu.p3do.clio.EIFIFO``n``.RDREQ) begin \
		fpop[n]++; tpop[n]++; \
		if (emu.p3do.clio.EIFIFO_COUNT[n] == 0) begin fempty[n]++; tempty[n]++; end \
	end
	always @(posedge clk_sys) begin
		bit vbl_o;
		if (fifo_stats) begin
			`FIFO_UNDER(0) `FIFO_UNDER(1) `FIFO_UNDER(2) `FIFO_UNDER(3) `FIFO_UNDER(4) `FIFO_UNDER(5) `FIFO_UNDER(6)
			`FIFO_UNDER(7) `FIFO_UNDER(8) `FIFO_UNDER(9) `FIFO_UNDER(10) `FIFO_UNDER(11) `FIFO_UNDER(12)
			if (emu.pause) begin pause_run++; if (pause_run > pause_max) pause_max = pause_run; end
			else pause_run = 0;
			vbl_o <= emu.VBL_N;
			if (vbl_o && !emu.VBL_N && vframe % 60 == 59) begin
				$write("[fifo f%0d] longest pause %0d cycles |", vframe, pause_max);
				for (int i = 0; i < 13; i++)
					if (fpop[i]) $write(" ch%0d reads %0d empty %0d", i, fpop[i], fempty[i]);
				$write("\n");
				for (int i = 0; i < 13; i++) begin fpop[i] = 0; fempty[i] = 0; end
				pause_max = 0;
			end
		end
	end
	final if (fifo_stats) begin
		$write("[fifo total]");
		for (int i = 0; i < 13; i++)
			if (tpop[i]) $write(" ch%0d reads %0d empty %0d", i, tpop[i], tempty[i]);
		$write("\n");
	end

	// +ioread=F1 +ioread_end=F2: CPU reads of CLIO registers (DSP EO memory, timers, XBUS...) seen
	// by CLIO itself, frames F1..F2 (the CPU-side --watch misses reads followed by an internal cycle);
	// +ioread_lo=H +ioread_hi=H keep the registers in that range (hex offsets in CLIO)
	int ioread_f1 = -1, ioread_f2 = -1, ioread_n = 0, ioread_lo = 0, ioread_hi = 'hFFFF;
	initial begin
		if (!$value$plusargs("ioread=%d", ioread_f1)) ioread_f1 = -1;
		if (!$value$plusargs("ioread_end=%d", ioread_f2)) ioread_f2 = ioread_f1;
		if (!$value$plusargs("ioread_lo=%h", ioread_lo)) ioread_lo = 0;
		if (!$value$plusargs("ioread_hi=%h", ioread_hi)) ioread_hi = 'hFFFF;
	end
	always @(posedge clk_sys) begin
		bit pend;
		if (ioread_f1 >= 0 && vframe >= ioread_f1 && vframe <= ioread_f2 && ioread_n < 20000) begin
			if (emu.p3do.clio.EN && emu.p3do.clio.CE_R && emu.p3do.clio.IO_EN && emu.p3do.clio.IO_ST == 0 &&
			    emu.p3do.clio.CLC == 3'h3 && !emu.p3do.clio.INFO_CODE &&
			    int'({emu.p3do.clio.A, 2'b00}) >= ioread_lo && int'({emu.p3do.clio.A, 2'b00}) <= ioread_hi) begin
				pend <= 1;
				$write("[ioread f%0d] CLIO+%04X", vframe, {emu.p3do.clio.A, 2'b00});
			end
			else if (pend) begin
				pend <= 0;
				ioread_n++;
				$write(" = %08X\n", emu.p3do.clio.CPU_DO);
			end
		end
	end

	// +madread=F1 +madread_end=F2: CPU reads of MADAM registers (DMA stack, cel engine...) in frames
	// F1..F2, with the value MADAM returns
	int madread_f1 = -1, madread_f2 = -1, madread_n = 0;
	initial begin
		if (!$value$plusargs("madread=%d", madread_f1)) madread_f1 = -1;
		if (!$value$plusargs("madread_end=%d", madread_f2)) madread_f2 = madread_f1;
	end
	always @(posedge clk_sys) begin
		if (madread_f1 >= 0 && vframe >= madread_f1 && vframe <= madread_f2 && madread_n < 20000 &&
		    emu.p3do.MCLK_PH2 && !emu.pause && emu.p3do.madam.CPU_REG_SEL && !emu.p3do.CPU_nRW) begin
			madread_n++;
			$display("[madread f%0d] MADAM+%04X = %08X", vframe, emu.p3do.madam.A[15:0], emu.p3do.madam.REG_DO);
		end
	end

	// +cd_trace: commands of the 3DO OS to the drive, requests of the drive to Main_MiSTer
	bit cd_trace = 0;
	longint sys_cycles = 0;
	initial cd_trace = $test$plusargs("cd_trace");
	always @(posedge clk_sys) begin
		bit host_old;
		bit [2:0] st_old;
		sys_cycles <= sys_cycles + 1;
		if (cd_trace) begin
			if (st_old == 3'd0 && emu.cdrom.CMD_ST != 3'd0)     // a command is executed
				$display("[cd %.3f s] cmd %02X %02X %02X %02X %02X %02X %02X  status %02X", real'(sys_cycles) / 50.0e6,
				         emu.cdrom.COMMAND[0], emu.cdrom.COMMAND[1], emu.cdrom.COMMAND[2], emu.cdrom.COMMAND[3],
				         emu.cdrom.COMMAND[4], emu.cdrom.COMMAND[5], emu.cdrom.COMMAND[6], emu.cdrom.STATUS);
			if (emu.cdrom.CD_IN_MSB != host_old)
				$display("[cd] to Main: %02X %02X %02X %02X %02X %02X %02X %02X",
				         emu.cdrom.HOST_COMM[0], emu.cdrom.HOST_COMM[1], emu.cdrom.HOST_COMM[2], emu.cdrom.HOST_COMM[3],
				         emu.cdrom.HOST_COMM[4], emu.cdrom.HOST_COMM[5], emu.cdrom.HOST_COMM[6], emu.cdrom.HOST_COMM[7]);
		end
		host_old = emu.cdrom.CD_IN_MSB;
		st_old = emu.cdrom.CMD_ST;
	end

	// replies of the drive (status bytes read by the host, until ST_EN falls), with +cd_trace
	bit [7:0] reply [16];
	int reply_n = 0;
	bit st_en_old;
	always @(posedge clk_sys) begin
		if (cd_trace) begin
			if (emu.cdrom.IO_ST == 1) begin      // IO_CMD_READ: the host reads a status byte
				reply[reply_n[3:0]] = emu.cdrom.STAT_DATA[emu.cdrom.STAT_FIFO_RPOS];
				reply_n++;
			end
			if (st_en_old && !emu.cdrom.ST_EN && reply_n) begin
				$write("[cd] reply:");
				for (int i = 0; i < reply_n && i < 16; i++) $write(" %02X", reply[i]);
				$write("\n");
				reply_n = 0;
			end
		end
		st_en_old <= emu.cdrom.ST_EN;
	end

	// The 16-bit data bus is not simulated as a tristate net (Verilator resolves it wrongly between
	// the controller and the chip): the chip takes the data the controller drives
	// (emu.sdram1.sim_dq, see run.sh) and the controller reads sdram_dq_chip.
	wire [15:0] sdram_dq_chip;
	wire        sdram_dq_chip_oe;
	sdram_chip sdram_chip
	(
		.clk(clk_mem),
		.A(SDRAM_A),
		.BA(SDRAM_BA),
		.DQ_IN(emu.sdram1.sim_dq),
		.DQ_OUT(sdram_dq_chip),
		.DQ_OE(sdram_dq_chip_oe),
		.DQML(SDRAM_DQML),
		.DQMH(SDRAM_DQMH),
		.nCS(SDRAM_nCS),
		.nRAS(SDRAM_nRAS),
		.nCAS(SDRAM_nCAS),
		.nWE(SDRAM_nWE)
	);

	ddr3_avalon ddr3
	(
		.clk(clk_mem),
		.addr(DDRAM_ADDR),
		.burstcnt(DDRAM_BURSTCNT),
		.rd(DDRAM_RD),
		.we(DDRAM_WE),
		.din(DDRAM_DIN),
		.be(DDRAM_BE),
		.busy(DDRAM_BUSY),
		.dout(DDRAM_DOUT),
		.dout_ready(DDRAM_DOUT_READY)
	);
endmodule

// ---------------------------------------------------------------------------------------------
// SDR SDRAM chip, 16-bit, 4 banks, single-word accesses (the controller of this core uses a
// burst length of 1). Commands are taken on the clk_mem edge after the controller sets the
// pins (on the board, the SDRAM clock is inverted: it takes them half a cycle after). With
// the CAS latency of 2 SDRAM clocks, the read data reaches the controller on the third clk_mem
// edge after its pins: here it is driven on the edge after the command is taken (CL = 1 edge).
// Checked with the BIOS memory probe: one edge more and the left word reads as undriven.
module sdram_chip #(parameter CL = 1)
(
	input             clk,
	input      [12:0] A,
	input       [1:0] BA,
	input      [15:0] DQ_IN,       // data driven by the controller
	output reg [15:0] DQ_OUT,      // read data, valid when DQ_OE
	output reg        DQ_OE = 0,
	input             DQML,
	input             DQMH,
	input             nCS,
	input             nRAS,
	input             nCAS,
	input             nWE
);
	reg [15:0] mem [0:(1<<24)-1];     // bank(2) row(12) column(10)
	reg [11:0] row [4];

	reg [15:0] rd_data [CL];
	reg        rd_valid [CL];
	wire [15:0] DQ = DQ_IN;

	// +sdram_trace: commands on row 0, columns 0-7 (the first 16 bytes of each bank)
	bit trace = 0;
	int traced = 0;
	longint t = 0;
	initial trace = $test$plusargs("sdram_trace");
	always @(posedge clk) begin
		t <= t + 1;
		if (trace && !nCS && traced < 300) begin
			case ({nRAS, nCAS, nWE})
				3'b011: if (A[11:0] == 0) begin $display("[sdram %0d] ACT   bank %0d row %03X", t, BA, A[11:0]); traced++; end
				3'b101: if (row[BA] == 0 && A[9:0] < 8) begin
					$display("[sdram %0d] READ  bank %0d col %0d -> %04X", t, BA, A[9:0], mem[{BA, row[BA], A[9:0]}]); traced++; end
				3'b100: if (row[BA] == 0 && A[9:0] < 8) begin
					$display("[sdram %0d] WRITE bank %0d col %0d <- %04X dqm %b%b", t, BA, A[9:0], DQ, DQMH, DQML); traced++; end
				default: ;
			endcase
		end
	end

	always @(posedge clk) begin
		for (int i = CL-1; i > 0; i--) begin
			rd_data[i] <= rd_data[i-1];
			rd_valid[i] <= rd_valid[i-1];
		end
		rd_valid[0] <= 0;
		DQ_OUT <= rd_data[CL-1];
		DQ_OE <= rd_valid[CL-1];

		if (!nCS) begin
			case ({nRAS, nCAS, nWE})
				3'b011: row[BA] <= A[11:0];                                       // ACTIVE
				3'b101: begin                                                     // READ
					rd_data[0] <= mem[{BA, row[BA], A[9:0]}];
					rd_valid[0] <= 1;
				end
				3'b100: begin                                                     // WRITE
					if (!DQML) mem[{BA, row[BA], A[9:0]}][ 7:0] <= DQ[ 7:0];
					if (!DQMH) mem[{BA, row[BA], A[9:0]}][15:8] <= DQ[15:8];
				end
				default: ;                                                        // PRECHARGE, REFRESH, MODE
			endcase
		end
	end
endmodule

// ---------------------------------------------------------------------------------------------
// DDR3 behind the HPS (Avalon-MM slave, 64-bit words). Reads are answered after LAT clk_mem
// cycles (+ddr_lat=N, default 16), one word per cycle; writes are taken at once. The core uses
// the first 8 MB above 0x30000000.
module ddr3_avalon #(parameter AW = 20)
(
	input             clk,
	input      [28:0] addr,
	input       [7:0] burstcnt,
	input             rd,
	input             we,
	input      [63:0] din,
	input       [7:0] be,
	output            busy,
	output reg [63:0] dout,
	output reg        dout_ready
);
	reg [63:0] mem [0:(1<<AW)-1];
	assign busy = 1'b0;

	int lat = 16;
	// +ddr_jitter=N: 0..N more cycles on each read; +ddr_spike_pm=P +ddr_spike=L: P reads per
	// thousand wait L more cycles (the MiSTer's DDR3 is shared with the HPS and the video scaler)
	int jitter = 0, spike_pm = 0, spike = 0;
	initial begin
		if (!$value$plusargs("ddr_lat=%d", lat)) lat = 16;
		if (!$value$plusargs("ddr_jitter=%d", jitter)) jitter = 0;
		if (!$value$plusargs("ddr_spike_pm=%d", spike_pm)) spike_pm = 0;
		if (!$value$plusargs("ddr_spike=%d", spike)) spike = 0;
	end
	function automatic longint extra_lat();
		longint e = 0;
		if (jitter > 0) e += longint'($urandom % (jitter + 1));
		if (spike_pm > 0 && ($urandom % 1000) < spike_pm) e += longint'(spike);
		return e;
	endfunction

	// pending reads
	reg [AW-1:0] q_addr [16];
	reg    [7:0] q_cnt  [16];
	longint      q_time [16];
	reg    [3:0] q_wr = 0, q_rd = 0;
	longint      now = 0;

	reg [AW-1:0] cur_addr;
	reg    [7:0] cur_left = 0;

	reg [AW-1:0] wr_addr;
	reg    [7:0] wr_left = 0;

	always @(posedge clk) begin
		now <= now + 1;
		dout_ready <= 0;

		if (we) begin
			reg [AW-1:0] a;
			a = wr_left ? wr_addr : addr[AW-1:0];
			for (int b = 0; b < 8; b++) if (be[b]) mem[a][b*8 +: 8] <= din[b*8 +: 8];
			wr_addr <= a + 1'd1;
			wr_left <= wr_left ? wr_left - 1'd1 : burstcnt - 1'd1;
		end

		if (rd) begin
			q_addr[q_wr] <= addr[AW-1:0];
			q_cnt[q_wr]  <= burstcnt;
			q_time[q_wr] <= now + longint'(lat) + extra_lat();
			q_wr <= q_wr + 1'd1;
		end

		if (cur_left) begin
			dout <= mem[cur_addr];
			dout_ready <= 1;
			cur_addr <= cur_addr + 1'd1;
			cur_left <= cur_left - 1'd1;
		end
		else if (q_rd != q_wr && now >= q_time[q_rd]) begin
			dout <= mem[q_addr[q_rd]];
			dout_ready <= 1;
			cur_addr <= q_addr[q_rd] + 1'd1;
			cur_left <= q_cnt[q_rd] - 1'd1;
			q_rd <= q_rd + 1'd1;
		end
	end
endmodule
