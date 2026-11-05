// Simulation models of the Intel blocks, VHDL helpers and MiSTer framework modules that 3DO.sv
// uses. Verilator cannot read the Intel simulation libraries nor VHDL, so each model reproduces
// only the configurations this core uses (see README.md).

// ---------------------------------------------------------------------------------------------
// altdpram (MLAB): every instance of the core registers the write side on inclock and reads
// asynchronously (rdaddress_reg = outdata_reg = "UNREGISTERED").
module altdpram #(
	parameter width = 1, widthad = 1, numwords = 0, width_byteena = 1, byte_size = 8,
	parameter indata_aclr = "OFF", indata_reg = "INCLOCK", intended_device_family = "",
	parameter lpm_type = "altdpram", lpm_hint = "", outdata_aclr = "OFF", outdata_reg = "UNREGISTERED",
	parameter ram_block_type = "", rdaddress_aclr = "OFF", rdaddress_reg = "UNREGISTERED",
	parameter rdcontrol_aclr = "OFF", rdcontrol_reg = "UNREGISTERED",
	parameter read_during_write_mode_mixed_ports = "", power_up_uninitialized = "FALSE",
	parameter wraddress_aclr = "OFF", wraddress_reg = "INCLOCK", wrcontrol_aclr = "OFF",
	parameter wrcontrol_reg = "INCLOCK"
)(
	input      [width-1:0]         data,
	input                          inclock,
	input                          inclocken,
	input                          outclock,
	input                          outclocken,
	input      [widthad-1:0]       rdaddress,
	input                          rdaddressstall,
	input                          rden,
	input      [widthad-1:0]       wraddress,
	input                          wraddressstall,
	input                          wren,
	input                          aclr,
	input                          sclr,
	input      [width_byteena-1:0] byteena,
	output     [width-1:0]         q
);
	localparam N = numwords ? numwords : (1 << widthad);
	localparam BW = width / width_byteena;
	reg [width-1:0] mem [0:N-1];

	always @(posedge inclock) begin
		if (wren) begin
			// a 1-bit byteena is the whole word (tied to 1 or left unconnected in this core)
			if (width_byteena == 1) mem[wraddress] <= data;
			else for (int b = 0; b < width_byteena; b++)
				if (byteena[b]) mem[wraddress][b*BW +: BW] <= data[b*BW +: BW];
		end
	end
	assign q = mem[rdaddress];
endmodule

// ---------------------------------------------------------------------------------------------
// altsyncram (M10K): DUAL_PORT or BIDIR_DUAL_PORT, unregistered outputs (one cycle of latency
// from the address register), ports of different widths, new data on a read during a write.
// The memory is kept as words of the narrower port; the lowest narrow address is the least
// significant part of a wide word, as in the Intel blocks.
module altsyncram #(
	parameter operation_mode = "BIDIR_DUAL_PORT",
	parameter width_a = 8, widthad_a = 8, numwords_a = 0,
	parameter width_b = 8, widthad_b = 8, numwords_b = 0,
	parameter width_byteena_a = 1, width_byteena_b = 1, byte_size = 8,
	parameter outdata_reg_a = "UNREGISTERED", outdata_reg_b = "UNREGISTERED",
	parameter address_reg_b = "CLOCK1", indata_reg_b = "CLOCK1", wrcontrol_wraddress_reg_b = "CLOCK1",
	parameter rdcontrol_reg_b = "CLOCK1", byteena_reg_b = "CLOCK1",
	parameter clock_enable_input_a = "BYPASS", clock_enable_input_b = "BYPASS",
	parameter clock_enable_output_a = "BYPASS", clock_enable_output_b = "BYPASS",
	parameter address_aclr_a = "NONE", address_aclr_b = "NONE",
	parameter outdata_aclr_a = "NONE", outdata_aclr_b = "NONE",
	parameter read_during_write_mode_port_a = "", read_during_write_mode_port_b = "",
	parameter read_during_write_mode_mixed_ports = "",
	parameter intended_device_family = "", lpm_type = "altsyncram", lpm_hint = "",
	parameter ram_block_type = "AUTO", power_up_uninitialized = "FALSE",
	parameter init_file = "", init_file_layout = "", enable_ecc = "FALSE"
)(
	input      [widthad_a-1:0]       address_a,
	input      [widthad_b-1:0]       address_b,
	input                            clock0,
	input                            clock1,
	input      [width_a-1:0]         data_a,
	input      [width_b-1:0]         data_b,
	input                            wren_a,
	input                            wren_b,
	input                            rden_a,
	input                            rden_b,
	input      [width_byteena_a-1:0] byteena_a,
	input      [width_byteena_b-1:0] byteena_b,
	input                            aclr0,
	input                            aclr1,
	input                            addressstall_a,
	input                            addressstall_b,
	input                            clocken0,
	input                            clocken1,
	input                            clocken2,
	input                            clocken3,
	output reg [width_a-1:0]         q_a,
	output reg [width_b-1:0]         q_b,
	output     [2:0]                 eccstatus
);
	localparam W   = width_a < width_b ? width_a : width_b;
	localparam RA  = width_a / W;
	localparam RB  = width_b / W;
	localparam NA  = numwords_a ? numwords_a : (1 << widthad_a);
	localparam N   = NA * RA;
	localparam BWA = width_a / width_byteena_a;
	localparam BWB = width_b / width_byteena_b;
	localparam BIDIR = operation_mode == "BIDIR_DUAL_PORT";

	reg [W-1:0] mem [0:N-1];
	assign eccstatus = '0;

	always @(posedge clock0) begin
		reg [width_a-1:0] w;
		for (int i = 0; i < RA; i++) w[i*W +: W] = mem[address_a*RA + i];
		if (wren_a) begin
			for (int b = 0; b < width_byteena_a; b++)
				if (width_byteena_a == 1 || byteena_a[b]) w[b*BWA +: BWA] = data_a[b*BWA +: BWA];
			for (int i = 0; i < RA; i++) mem[address_a*RA + i] <= w[i*W +: W];
		end
		q_a <= w;
	end

	always @(posedge clock1) begin
		reg [width_b-1:0] w;
		for (int i = 0; i < RB; i++) w[i*W +: W] = mem[address_b*RB + i];
		if (BIDIR && wren_b) begin
			for (int b = 0; b < width_byteena_b; b++)
				if (width_byteena_b == 1 || byteena_b[b]) w[b*BWB +: BWB] = data_b[b*BWB +: BWB];
			for (int i = 0; i < RB; i++) mem[address_b*RB + i] <= w[i*W +: W];
		end
		q_b <= w;
	end
endmodule

// ---------------------------------------------------------------------------------------------
// lpm_divide: combinational signed division with LPM_REMAINDERPOSITIVE=TRUE (the remainder is
// in [0, |denom|) and numer = quotient * denom + remain).
module lpm_divide #(
	parameter lpm_widthn = 64, lpm_widthd = 32, lpm_pipeline = 0,
	parameter lpm_nrepresentation = "SIGNED", lpm_drepresentation = "SIGNED",
	parameter lpm_hint = "", lpm_type = "LPM_DIVIDE"
)(
	input      [lpm_widthn-1:0] numer,
	input      [lpm_widthd-1:0] denom,
	input                       clock,
	input                       clken,
	input                       aclr,
	output reg [lpm_widthn-1:0] quotient,
	output reg [lpm_widthd-1:0] remain
);
	always_comb begin
		longint n, d, q, r;
		n = $signed(numer);
		d = $signed(denom);
		if (d == 0) begin
			q = -1;
			r = n;
		end else begin
			q = n / d;
			r = n % d;
			if (r < 0) begin
				if (d > 0) begin q = q - 1; r = r + d; end
				else       begin q = q + 1; r = r - d; end
			end
		end
		quotient = q[lpm_widthn-1:0];
		remain = r[lpm_widthd-1:0];
	end
endmodule

// ---------------------------------------------------------------------------------------------
// altddio_out: only drives SDRAM_CLK, which the SDRAM model does not use.
module altddio_out #(
	parameter extend_oe_disable = "OFF", intended_device_family = "", invert_output = "OFF",
	parameter lpm_hint = "", lpm_type = "altddio_out", oe_reg = "UNREGISTERED",
	parameter power_up_high = "OFF", width = 1
)(
	input  [width-1:0] datain_h,
	input  [width-1:0] datain_l,
	input              outclock,
	output [width-1:0] dataout,
	input              aclr,
	input              aset,
	input              oe,
	input              outclocken,
	input              sclr,
	input              sset
);
	assign dataout = outclock ? datain_h : datain_l;
endmodule

// ---------------------------------------------------------------------------------------------
// dpram_dif (rtl/bram.vhd): altsyncram BIDIR_DUAL_PORT on one clock. The enables and chip
// selects are left at their default (1) by every instance of this core.
module dpram_dif #(parameter addr_width_a = 8, data_width_a = 8, addr_width_b = 8, data_width_b = 8,
                   parameter mem_init_file = " ")
(
	input                         clock,
	input      [addr_width_a-1:0] address_a,
	input      [data_width_a-1:0] data_a,
	input                         enable_a,
	input                         wren_a,
	output     [data_width_a-1:0] q_a,
	input                         cs_a,
	input      [addr_width_b-1:0] address_b,
	input      [data_width_b-1:0] data_b,
	input                         enable_b,
	input                         wren_b,
	output     [data_width_b-1:0] q_b,
	input                         cs_b
);
	altsyncram #(
		.operation_mode("BIDIR_DUAL_PORT"),
		.width_a(data_width_a), .widthad_a(addr_width_a), .numwords_a(2**addr_width_a),
		.width_b(data_width_b), .widthad_b(addr_width_b), .numwords_b(2**addr_width_b)
	) ram (
		.address_a(address_a), .address_b(address_b), .clock0(clock), .clock1(clock),
		.data_a(data_a), .data_b(data_b), .wren_a(wren_a), .wren_b(wren_b),
		.rden_a(1'b1), .rden_b(1'b1), .byteena_a(1'b1), .byteena_b(1'b1),
		.aclr0(1'b0), .aclr1(1'b0), .addressstall_a(1'b0), .addressstall_b(1'b0),
		.clocken0(1'b1), .clocken1(1'b1), .clocken2(1'b1), .clocken3(1'b1),
		.q_a(q_a), .q_b(q_b), .eccstatus()
	);
endmodule

// ---------------------------------------------------------------------------------------------
// CEGen (rtl/CEGen.vhd): clock enable at OUT_CLK out of IN_CLK.
module CEGen
(
	input        CLK,
	input        RST_N,
	input [31:0] IN_CLK,
	input        IN_CE,
	input [31:0] OUT_CLK,
	output reg   CE
);
	int sum = 0;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			sum = 0;
			CE <= 0;
		end else begin
			CE <= 0;
			if (IN_CE) begin
				sum = sum + int'(OUT_CLK);
				if (sum >= int'(IN_CLK)) begin
					sum = sum - int'(IN_CLK);
					CE <= 1;
				end
			end
		end
	end
endmodule

// ---------------------------------------------------------------------------------------------
// PLLs: the clocks come from the C++ harness. clk_sys (outclk_0) is the 50 MHz input, clk_mem is
// sim_top.clk_mem (100 MHz, rising with clk_sys). The video PLL (49.0908 MHz on the board) is
// replaced by clk_sys: the video timing of the simulation is 1.8% faster than the console's.
module pll
(
	input  refclk,
	input  rst,
	output outclk_0,
	output outclk_1,
	output outclk_2,
	output locked
);
	assign outclk_0 = refclk;
	assign outclk_1 = sim_top.clk_mem;
	assign outclk_2 = 1'b0;
	assign locked = 1'b1;
endmodule

module pll2
(
	input         refclk,
	input         rst,
	output        outclk_0,
	input  [63:0] reconfig_to_pll,
	output [63:0] reconfig_from_pll,
	output        locked
);
	assign outclk_0 = refclk;
	assign reconfig_from_pll = '0;
	assign locked = 1'b1;
endmodule

module pll_cfg
(
	input         mgmt_clk,
	input         mgmt_reset,
	output        mgmt_waitrequest,
	input         mgmt_read,
	output [31:0] mgmt_readdata,
	input         mgmt_write,
	input  [5:0]  mgmt_address,
	input  [31:0] mgmt_writedata,
	output [63:0] reconfig_to_pll,
	input  [63:0] reconfig_from_pll
);
	assign mgmt_waitrequest = 1'b0;
	assign mgmt_readdata = '0;
	assign reconfig_to_pll = '0;
endmodule

// ---------------------------------------------------------------------------------------------
// Video output of the MiSTer framework: the picture is passed through unchanged (no scaler,
// scandoubler nor gamma), one pixel per CE_PIXEL.
module video_mixer #(parameter LINE_LENGTH = 768, HALF_DEPTH = 0, GAMMA = 0)
(
	input            CLK_VIDEO,
	output reg       CE_PIXEL,
	input            ce_pix,
	input            scandoubler,
	input            hq2x,
	inout     [21:0] gamma_bus,
	input      [7:0] R,
	input      [7:0] G,
	input      [7:0] B,
	input            HSync,
	input            VSync,
	input            HBlank,
	input            VBlank,
	input            HDMI_FREEZE,
	output           freeze_sync,
	output reg [7:0] VGA_R,
	output reg [7:0] VGA_G,
	output reg [7:0] VGA_B,
	output reg       VGA_VS,
	output reg       VGA_HS,
	output reg       VGA_DE
);
	assign freeze_sync = 1'b0;
	always @(posedge CLK_VIDEO) begin
		CE_PIXEL <= ce_pix;
		if (ce_pix) begin
			{VGA_R, VGA_G, VGA_B} <= {R, G, B};
			VGA_HS <= HSync;
			VGA_VS <= VSync;
			VGA_DE <= ~(HBlank | VBlank);
		end
	end
endmodule

module video_freak
(
	input             CLK_VIDEO,
	input             CE_PIXEL,
	input             VGA_VS,
	input      [11:0] HDMI_WIDTH,
	input      [11:0] HDMI_HEIGHT,
	output            VGA_DE,
	output reg [12:0] VIDEO_ARX,
	output reg [12:0] VIDEO_ARY,
	input             VGA_DE_IN,
	input      [11:0] ARX,
	input      [11:0] ARY,
	input      [11:0] CROP_SIZE,
	input       [4:0] CROP_OFF,
	input       [2:0] SCALE
);
	assign VGA_DE = VGA_DE_IN;
	always @(posedge CLK_VIDEO) begin
		VIDEO_ARX <= {1'b0, ARX};
		VIDEO_ARY <= {1'b0, ARY};
	end
endmodule
