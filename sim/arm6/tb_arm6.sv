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
//  Interrupts: a write to 0xF0000010 (IRQ) or 0xF0000014 (FIQ) releases the
//  line when the value is 0, or asserts it after that many CPU cycles. With
//  +IRQRATE/+FIQRATE the line is also asserted at random, on average that many
//  CPU cycles (plus +INTMIN) after the program released it. INTMIN must cover
//  the end of the handlers: a line asserted again before the return makes the
//  next interrupt come before any instruction of the program.
//
//  Reset: a write of n > 0 to 0xF0000018 drives nRESET low n CPU cycles later,
//  for 8 cycles (the 3DO's CLIO soft reset). The RAM keeps its contents.
//
//  Plusargs:  +IMAGE=<file.memh>  one 32-bit word per line, as seen on the bus
//             +MAXCYC=<n>         clock cycle limit (default 200000)
//             +DUMPADDR=<hex>     memory printed at the end (default 2000)
//             +DUMPLEN=<n>        number of words printed (default 16)
//             +IRQRATE=<n>        random IRQs (default 0: none)
//             +FIQRATE=<n>        random FIQs (default 0: none)
//             +INTSEED=<n>        seed of the random interrupts (default 1)
//             +INTMIN=<n>         minimum delay of the random interrupts (default 0)
//  Every line printed by the bench starts with "TB " (parsed by run.sh).
//============================================================================
module tb_arm6 #(parameter bit BIGEND = 1'b1);	// the 3DO runs the ARM60 big-endian (see P3DO.sv)

	reg           clk = 0, rst_n = 0, phase = 0;
	wire   [31:0] adr, wdat, rdat;
	wire          nrw, nwb, nmreq, seq, nopc, ntrans, lock, ili;

	reg    [31:0] mem [0:16383];
	reg    [31:0] res [0:3];
	reg [8*256:1] image;
	integer       i, maxcyc, dumpaddr, dumplen, cycles = 0;
	wire    [1:0] lane = adr[1:0] ^ {2{BIGEND}};

	reg           irq_n = 1, fiq_n = 1, cpures_n = 1;
	integer       irqrate, fiqrate, intmin, irq_wait = 0, fiq_wait = 0, irqs = 0, fiqs = 0;
	integer       res_wait = 0, res_len = 0, resets = 0;
	reg    [31:0] rnd;

	assign rdat = adr < 32'h00010000 ? mem[adr[15:2]] : 32'h00000000;

	ARM6_CORE dut (
		.CLK(clk), .RST_N(rst_n), .EN(1'b1), .BIGEND(BIGEND),
		.CE_R(phase), .CE_F(~phase),
		.nRESET(cpures_n), .nFIQ(fiq_n), .nIRQ(irq_n),
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
		if (!$value$plusargs("DUMPADDR=%h", dumpaddr)) dumpaddr = 32'h2000;
		if (!$value$plusargs("DUMPLEN=%d", dumplen)) dumplen = 16;
		if (!$value$plusargs("IRQRATE=%d", irqrate)) irqrate = 0;
		if (!$value$plusargs("FIQRATE=%d", fiqrate)) fiqrate = 0;
		if (!$value$plusargs("INTSEED=%d", rnd)) rnd = 1;
		if (!$value$plusargs("INTMIN=%d", intmin)) intmin = 0;
		if (rnd == 0) rnd = 1;
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
			$display("TB INTS irq=%0d fiq=%0d", irqs, fiqs);
			$display("TB TIMEOUT cycles=%0d adr=%08x inst=%08x", cycles, adr, dut.EX_IC);
			$finish;
		end
		if (rst_n && !phase) begin
			// Interrupt lines: countdown to assertion, random rearming once released
			rnd = rnd ^ (rnd << 13);
			rnd = rnd ^ (rnd >> 17);
			rnd = rnd ^ (rnd << 5);
			if (irq_wait != 0) begin
				if (irq_wait == 1) begin irq_n <= 0; irqs <= irqs + 1; end
				irq_wait <= irq_wait - 1;
			end else if (irq_n && irqrate != 0) irq_wait <= 1 + intmin + rnd[15:0] % (2 * irqrate);
			if (fiq_wait != 0) begin
				if (fiq_wait == 1) begin fiq_n <= 0; fiqs <= fiqs + 1; end
				fiq_wait <= fiq_wait - 1;
			end else if (fiq_n && fiqrate != 0) fiq_wait <= 1 + intmin + rnd[31:16] % (2 * fiqrate);
			if (res_wait != 0) begin
				if (res_wait == 1) begin cpures_n <= 0; res_len <= 8; resets <= resets + 1; end
				res_wait <= res_wait - 1;
			end else if (res_len != 0) begin
				if (res_len == 1) cpures_n <= 1;
				res_len <= res_len - 1;
			end

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
						$display("TB INTS irq=%0d fiq=%0d", irqs, fiqs);
						if (resets != 0) $display("TB RESETS %0d", resets);
						for (i = 0; i < dumplen; i = i + 1) $display("TB DUMP %0d %08x", i, mem[(dumpaddr >> 2) + i]);
						$finish;
					end
				end else if (adr == 32'hF0000010) begin
					if (wdat == 0) begin irq_n <= 1; irq_wait <= 0; end
					else irq_wait <= wdat;
				end else if (adr == 32'hF0000014) begin
					if (wdat == 0) begin fiq_n <= 1; fiq_wait <= 0; end
					else fiq_wait <= wdat;
				end else if (adr == 32'hF0000018) begin
					res_wait <= wdat;
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
