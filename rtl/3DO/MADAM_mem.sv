module MADAM_MATH_MATRIX (
	input             CLK,
	
	input     [ 3: 0] WA,
	input     [31: 0] DIN,
	input             WE,
	
	input     [ 3: 0] RA,
	output    [31: 0] DOUT
);
		
`ifdef SIM

	reg [31:0] BUF[16];
	initial begin
		BUF = '{16{'0}};
	end
	
	always @(posedge CLK) begin
		if (WE) begin
			BUF[WA] <= DIN;
		end
	end

	assign DOUT = BUF[RA];
											  	
`else 

	wire [31: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RA),
				.wraddress (WA),
				.wren (WE),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 32,
		altdpram_component.widthad = 4,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule

module MADAM_MATH_VECTOR (
	input             CLK,
	
	input     [ 1: 0] WA,
	input     [31: 0] DIN,
	input             WE,
	
	input     [ 1: 0] RA,
	output    [31: 0] DOUT
);
		
`ifdef SIM

	reg [31:0] BUF[4];
	initial begin
		BUF = '{4{'0}};
	end
	
	always @(posedge CLK) begin
		if (WE) begin
			BUF[WA] <= DIN;
		end
	end

	assign DOUT = BUF[RA];
											  	
`else 

	wire [31: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RA),
				.wraddress (WA),
				.wren (WE),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 32,
		altdpram_component.widthad = 2,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule

module MADAM_MATH_STACK (
	input             CLK,
	input             EN,
	
	input     [ 2: 0] WA,
	input     [31: 0] DIN,
	input     [ 1: 0] WE,
	
	input     [ 2: 0] RA,
	output    [31: 0] DOUT
);
		
`ifdef SIM

	reg [15:0] XBUF[8],YBUF[8];
	initial begin
		XBUF = '{8{'0}};
		YBUF = '{8{'0}};
	end
	
	always @(posedge CLK) begin
		if (EN) begin
			if (WE[1]) begin
				XBUF[WA] <= DIN[31:16];
			end
			if (WE[0]) begin
				YBUF[WA] <= DIN[15:0];
			end
		end
	end

	assign DOUT = {XBUF[RA],YBUF[RA]};
											  	
`else 

	wire [31: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RA),
				.wraddress (WA),
				.wren (|WE),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena ({{2{WE[1]}},{2{WE[0]}}}),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 32,
		altdpram_component.widthad = 3,
		altdpram_component.width_byteena = 4,
		altdpram_component.byte_size = 8,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule

module madam_div (
	denom,
	numer,
	quotient,
	remain);

	input	[31:0]  denom;
	input	[63:0]  numer;
	output	[63:0]  quotient;
	output	[31:0]  remain;

	wire [63:0] sub_wire0;
	wire [31:0] sub_wire1;
	wire [63:0] quotient = sub_wire0[63:0];
	wire [31:0] remain = sub_wire1[31:0];

	lpm_divide	LPM_DIVIDE_component (
				.denom (denom),
				.numer (numer),
				.quotient (sub_wire0),
				.remain (sub_wire1),
				.aclr (1'b0),
				.clken (1'b1),
				.clock (1'b0));
	defparam
		LPM_DIVIDE_component.lpm_drepresentation = "SIGNED",
		LPM_DIVIDE_component.lpm_hint = "LPM_REMAINDERPOSITIVE=TRUE",
		LPM_DIVIDE_component.lpm_nrepresentation = "SIGNED",
		LPM_DIVIDE_component.lpm_type = "LPM_DIVIDE",
		LPM_DIVIDE_component.lpm_widthd = 32,
		LPM_DIVIDE_component.lpm_widthn = 64;

endmodule

//The DMA stack read a clock ahead (timing: the read of the memory and the choice of the word were on the path from the stack to
//the RAS of MADAM and the SDRAM): four banks of 32 words, one per low address bit pair, all read at the group the read address
//has after the clock (RGRP, from MADAM_AG), and the word of its low bits after the clock (RLOW) goes into Q, or the word written
//in that clock at that address. Q is the word a read of MADAM_DMA_STACK at the read address gives.
module MADAM_DMA_STACK_PR (
	input             CLK,
	input             CE,
	input             EN,

	input     [ 6: 0] WADDR,
	input     [21: 0] DIN,
	input             WE,

	input     [ 6: 2] RGRP,		//the read address after this clock: group (6:2)
	input     [ 1: 0] RLOW,		//and low bits
	output reg [21: 0] Q = '0
);

	wire         WR = WE && EN && CE;
	wire [87: 0] BQ;				//the four words of the group RGRP, word k in BQ[22*k+21:22*k]

	always @(posedge CLK) Q <= (WR && WADDR == {RGRP,RLOW}) ? DIN :
	                           RLOW[1] ? (RLOW[0] ? BQ[87:66] : BQ[65:44]) : (RLOW[0] ? BQ[43:22] : BQ[21:0]);

	genvar k;
	generate
		for (k = 0; k < 4; k = k + 1) begin : bank
			localparam bit [1:0] K = k;
			wire [21: 0] q;
			assign BQ[22*k +: 22] = q;
			altdpram #(
				.indata_aclr("OFF"),
				.indata_reg("INCLOCK"),
				.intended_device_family("Cyclone V"),
				.lpm_type("altdpram"),
				.outdata_aclr("OFF"),
				.outdata_reg("UNREGISTERED"),
				.ram_block_type("MLAB"),
				.rdaddress_aclr("OFF"),
				.rdaddress_reg("UNREGISTERED"),
				.rdcontrol_aclr("OFF"),
				.rdcontrol_reg("UNREGISTERED"),
				.read_during_write_mode_mixed_ports("CONSTRAINED_DONT_CARE"),
				.width(22),
				.widthad(5),
				.width_byteena(1),
				.wraddress_aclr("OFF"),
				.wraddress_reg("INCLOCK"),
				.wrcontrol_aclr("OFF"),
				.wrcontrol_reg("INCLOCK")
			) altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RGRP),
				.wraddress (WADDR[6:2]),
				.wren (WR && WADDR[1:0] == K),
				.q (q),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
				.wraddressstall (1'b0));
		end
	endgenerate

endmodule

module MADAM_DMA_STACK (
	input             CLK,
	input             CE,
	input             EN,
	
	input     [ 6: 0] WADDR,
	input     [21: 0] DIN,
	input             WE,
	
	input     [ 6: 0] RADDR,
	output    [21: 0] DOUT
);
	
// synopsys translate_off
`define SIM
// synopsys translate_on
	
`ifdef SIM

	reg [21:0]  BUF[128];
	initial begin
		BUF <= '{128{'0}};
		BUF[7'h60] <= 22'h080000;
	end
	
	always @(posedge CLK) begin
		if (EN) begin
			if (WE && CE) begin
				BUF[WADDR] <= DIN;
			end
		end
	end

	assign DOUT = BUF[RADDR];
											  	
`else 

	wire [21: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RADDR),
				.wraddress (WADDR),
				.wren (WE && EN && CE),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 22,
		altdpram_component.widthad = 7,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;

`endif

endmodule

module MADAM_SPRYTE_DATA_FIFO (
	input             CLK,
	input             EN,
	input             RST,
	
	input     [15: 0] DIN,
	input             WRREQ,
	
	input             RDREQ,
	output    [15: 0] DOUT,
	
	output            FULL,
	output            LESSHALF,
	input      [ 4: 0] THR,	//experiment thr2_16: the refill threshold
	output            EMPTY
);

	bit  [ 3: 0] RADDR;
	bit  [ 3: 0] WADDR;
	bit  [ 4: 0] AMOUNT;
	
	always @(posedge CLK) begin
		if (RST) begin
			AMOUNT <= '0;
			RADDR <= '0;
			WADDR <= '0;
		end
		else if (EN) begin
			if (WRREQ && !AMOUNT[4]) begin
				WADDR <= WADDR + 4'd1;
			end
			if (RDREQ && AMOUNT) begin
				RADDR <= RADDR + 4'd1;
			end
			
			if (WRREQ && !RDREQ && !AMOUNT[4]) begin
				AMOUNT <= AMOUNT + 5'd1;
			end else if (!WRREQ && RDREQ && AMOUNT) begin
				AMOUNT <= AMOUNT - 5'd1;
			end
		end
	end
	assign FULL = AMOUNT[4];
	assign LESSHALF = (AMOUNT <= THR);	//experiment thr2_16	//FZ-10 (disc 3.3, CELS LARGEURS): the time per row steps at source words 5, 9, 13, not 2, 6, 10
	assign EMPTY = ~|AMOUNT;
	
`ifdef SIM

	reg [15:0] BUF[16];
	initial begin
		BUF = '{16{'0}};
	end
	
	always @(posedge CLK) begin
		if (EN) begin
			if (WRREQ) begin
				BUF[WADDR] <= DIN;
			end
		end
	end

	assign DOUT = BUF[RADDR];
											  	
`else 

	wire [15: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RADDR),
				.wraddress (WADDR),
				.wren (WRREQ && EN),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 16,
		altdpram_component.widthad = 4,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule


module MADAM_PIP (
	input             CLK,
	input             EN,
	
	input     [ 4: 0] WA,
	input     [15: 0] DIN,
	input             WE,
	
	input     [ 4: 0] RA,
	output    [15: 0] DOUT
);
		
`ifdef SIM

	reg [15:0] BUF[32];
	initial begin
		BUF = '{32{'0}};
	end
	
	always @(posedge CLK) begin
		if (EN) begin
			if (WE) begin
				BUF[WA] <= DIN;
			end
		end
	end

	assign DOUT = BUF[RA];
											  	
`else 

	wire [15: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RA),
				.wraddress (WA),
				.wren (WE),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = 16,
		altdpram_component.widthad = 5,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule

module MADAM_SYNC_FIFO 
# (parameter width = 16)
(
	input               CLK,
	input               EN,
	input               RST,
	
	input  [width-1: 0] DIN,
	input               WRREQ,
	
	input               RDREQ,
	output [width-1: 0] DOUT,
	
	output              FULL,
	output              LESSHALF,
	output              EMPTY
);

	bit  [ 3: 0] RADDR;
	bit  [ 3: 0] WADDR;
	bit  [ 4: 0] AMOUNT;
	
	always @(posedge CLK) begin
		if (RST) begin
			AMOUNT <= '0;
			RADDR <= '0;
			WADDR <= '0;
		end
		else if (EN) begin
			if (WRREQ && !AMOUNT[4]) begin
				WADDR <= WADDR + 4'd1;
			end
			if (RDREQ && AMOUNT) begin
				RADDR <= RADDR + 4'd1;
			end
			
			if (WRREQ && !RDREQ && !AMOUNT[4]) begin
				AMOUNT <= AMOUNT + 5'd1;
			end else if (!WRREQ && RDREQ && AMOUNT) begin
				AMOUNT <= AMOUNT - 5'd1;
			end
		end
	end
	assign FULL = AMOUNT >= 5'h0F;
	assign LESSHALF = AMOUNT <= 5'd3;
	assign EMPTY = ~|AMOUNT;
	
`ifdef SIM

	reg [width-1:0] BUF[16];
	initial begin
		BUF = '{16{'0}};
	end
	
	always @(posedge CLK) begin
		if (EN) begin
			if (WRREQ) begin
				BUF[WADDR] <= DIN;
			end
		end
	end

	assign DOUT = BUF[RADDR];
											  	
`else 

	wire [width-1: 0] sub_wire0;

	altdpram	altdpram_component (
				.data (DIN),
				.inclock (CLK),
				.rdaddress (RADDR),
				.wraddress (WADDR),
				.wren (WRREQ && EN),
				.q (sub_wire0),
				.aclr (1'b0),
				.byteena (1'b1),
				.inclocken (1'b1),
				.rdaddressstall (1'b0),
				.rden (1'b1),
//				.sclr (1'b0),
				.wraddressstall (1'b0));
	defparam
		altdpram_component.indata_aclr = "OFF",
		altdpram_component.indata_reg = "INCLOCK",
		altdpram_component.intended_device_family = "Cyclone V",
		altdpram_component.lpm_type = "altdpram",
		altdpram_component.outdata_aclr = "OFF",
		altdpram_component.outdata_reg = "UNREGISTERED",
		altdpram_component.ram_block_type = "MLAB",
		altdpram_component.rdaddress_aclr = "OFF",
		altdpram_component.rdaddress_reg = "UNREGISTERED",
		altdpram_component.rdcontrol_aclr = "OFF",
		altdpram_component.rdcontrol_reg = "UNREGISTERED",
		altdpram_component.read_during_write_mode_mixed_ports = "CONSTRAINED_DONT_CARE",
		altdpram_component.width = width,
		altdpram_component.widthad = 4,
		altdpram_component.width_byteena = 1,
		altdpram_component.wraddress_aclr = "OFF",
		altdpram_component.wraddress_reg = "INCLOCK",
		altdpram_component.wrcontrol_aclr = "OFF",
		altdpram_component.wrcontrol_reg = "INCLOCK";
		
	assign DOUT = sub_wire0;
	
`endif

endmodule
