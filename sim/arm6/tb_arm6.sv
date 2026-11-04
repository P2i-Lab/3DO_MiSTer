`timescale 1ns/1ps
//============================================================================
//  Standalone testbench for the ARM6 core (rtl/ARM6), for Verilator.
//
//  The CPU runs a program image from address 0 with a zero-wait-state 64 KB
//  RAM. The program reports completion by writing to 0xF0000000:
//    - self-checking tests (tests/*.S) write 0x0000600D on success, or the
//      failing test id, actual and expected values to 0xF0000004/8/C and
//      then 0x00000BAD;
//    - reproduction images (repro/*.memh) store their results at 0x2000.
//
//  Plusargs:  +IMAGE=<file.memh>  one 32-bit word per line, as seen on the bus
//             +MAXCYC=<n>         clock cycle limit (default 200000)
//  Every line printed by the bench starts with "TB " (parsed by run.sh).
//============================================================================
module tb_arm6 #(parameter bit BIGEND = 1'b1);	// the 3DO runs the ARM60 big-endian (see P3DO.sv)

	reg           clk = 0, rst_n = 0, phase = 0;
	wire   [31:0] adr, wdat, rdat;
	wire          nrw, nwb, nmreq, seq, nopc, ntrans, lock, ili;

	reg    [31:0] mem [0:16383];
	reg    [31:0] res [0:3];
	reg [8*256:1] image;
	integer       i, maxcyc, cycles = 0;
	wire    [1:0] lane = adr[1:0] ^ {2{BIGEND}};

	assign rdat = adr < 32'h00010000 ? mem[adr[15:2]] : 32'h00000000;

	ARM6_CORE dut (
		.CLK(clk), .RST_N(rst_n), .EN(1'b1), .BIGEND(BIGEND),
		.CE_R(phase), .CE_F(~phase),
		.nRESET(1'b1), .nFIQ(1'b1), .nIRQ(1'b1),
		.EDI(32'h00000000),
		.A(adr), .DI(rdat), .DO(wdat), .nRW(nrw), .nWB(nwb),
		.nMREQ(nmreq), .SEQ(seq), .nOPC(nopc), .nTRANS(ntrans), .LOCK(lock), .DBE(1'b1),
		.ILI(ili)
	);

	always #1 clk = ~clk;
	always @(negedge clk) phase <= ~phase;

	initial begin
		if (!$value$plusargs("IMAGE=%s", image)) $fatal(1, "TB missing +IMAGE=<file.memh>");
		if (!$value$plusargs("MAXCYC=%d", maxcyc)) maxcyc = 200000;
		for (i = 0; i < 16384; i = i + 1) mem[i] = 32'h00000000;
		for (i = 0; i < 4; i = i + 1) res[i] = 32'h00000000;
		$readmemh(image, mem);
`ifdef TRACE
		$dumpfile("trace.fst");
		$dumpvars(0, tb_arm6);
`endif
		#2.5 rst_n = 1;
	end

	always @(posedge clk) begin
		cycles <= cycles + 1;
		if (cycles >= maxcyc) begin
			$display("TB TIMEOUT cycles=%0d adr=%08x inst=%08x", cycles, adr, dut.EX_IC);
			$finish;
		end
		if (rst_n && !phase) begin
			if (ili) begin
				$display("TB ILLEGAL inst=%08x adr=%08x", dut.EX_IC, adr);
				$finish;
			end
			if (nrw) begin
				if (adr[31:4] == 28'hF000000) begin
					res[adr[3:2]] = wdat;
					if (adr[3:2] == 2'd0) begin
						$display("TB DONE marker=%08x id=%08x actual=%08x expected=%08x cycles=%0d", wdat, res[1], res[2], res[3], cycles);
						$display("TB MARKER %08x", wdat);
						for (i = 0; i < 16; i = i + 1) $display("TB DUMP %0d %08x", i, mem[14'h0800 + i]);
						$finish;
					end
				end else if (adr < 32'h00010000) begin
					if (nwb) mem[adr[15:2]] <= wdat;
					else     mem[adr[15:2]][lane*8 +: 8] <= wdat[7:0];
				end else begin
					$display("TB BADWRITE adr=%08x data=%08x", adr, wdat);
					$finish;
				end
			end
		end
	end

endmodule
