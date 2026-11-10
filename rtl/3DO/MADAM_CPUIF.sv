import P3DO_PKG::*; 

module MADAM_CPUIF
(
	input              CLK,
	input              RST_N,
	input              EN,
	
	input              CE_R,
	input              CE_F,
	
	input              PHASE1,
	input              PHASE2,
	
	input      [31: 0] A,
	input              nRW,
	input              nWB,
	input              nMREQ,
	input              SEQ,
	input              nOPC,
	input              nTRANS,
	input              LOCK,
	output             DBE,
	output             nRES,
	output             MCLK_PH1,
	output             MCLK_PH2,
	
	input              GRANT,
	output             READY,
	output             ACCESS,
	output             START,
	output reg [ 3: 0] WE,
	input              AG_PBI,
	output AddrGenCtl_t AG_CTL,
	
	input              SLOW_SEL,
	input              CLIO_SEL,
	input              CLIO_RDY,
	input              WAIT,
	
	output reg         MCLK
);

	wire CPU_PH1 = PHASE1 & CE_R;
	wire CPU_PH2 = PHASE2 & CE_R;
	
	bit         RESET;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			RESET <= 1;
		end
		else if (EN && CE_R) begin
			if (PHASE2) begin
				RESET <= 0;
			end
		end
	end 
	assign nRES = ~RESET;
	
	bit         SLOW_WAIT;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			SLOW_WAIT <= 0;
		end
		else if (EN && CE_F) begin
			if (PHASE1) begin
				if (SLOW_SEL && !SLOW_WAIT) SLOW_WAIT <= 1;
				else if (SLOW_WAIT && !WAIT) SLOW_WAIT <= 0;
			end
		end
	end 
	
	wire        CPU_SPORT = (A >= 32'h03200000 && A <= 32'h032FFFFF);
	wire        CPU_PB = AG_PBI && ~|A[31:24];
	bit         STRETCH;
	bit         EXTRA;
	bit         BREAK;
	bit         PAUSE;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			STRETCH <= 1;
			EXTRA <= 0;
			BREAK <= 0;
			PAUSE <= 0;
		end
		else if (EN && CE_F) begin
			if (PHASE1) begin
				if (CLIO_SEL && !PAUSE) PAUSE <= 1;
				if (CLIO_SEL && CLIO_RDY && !STRETCH && PAUSE) PAUSE <= 0;
			end
		end
		else if (EN && CE_R) begin
			if (PHASE2) begin
				//A non-sequential access to RAM takes one more bus frame than the two of the core: the FZ-10 spends about
				//60 ns more on it (bench 3.0, ARM TEMPS and MEMOIRE PAS). The frame comes before the RAS frame.
				if (!nMREQ && !SEQ && !SLOW_WAIT && !STRETCH && !EXTRA && A < 32'h00300000 && !nRW && !ICYCLE) EXTRA <= 1;
				else if (!nMREQ && !SEQ && !SLOW_WAIT && !STRETCH && !EXTRA) STRETCH <= 1;
				else if (EXTRA) begin EXTRA <= 0; STRETCH <= 1; end
				else if (GRANT) STRETCH <= 0;
				
				if (!GRANT && !BREAK) BREAK <= 1;
				else if (GRANT && CPU_PB && !(ICYCLE && SEQ) && !STRETCH && !EXTRA && !BREAK) BREAK <= 1;
				else if (GRANT && CPU_SPORT && !STRETCH && !EXTRA && !BREAK) STRETCH <= 1;
				else if (GRANT) BREAK <= 0;
			end
		end
	end 
	
	bit         NCYCLE;
	bit         SCYCLE;
	bit         ICYCLE,ICYCLE_PREV;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			NCYCLE <= 0;
			SCYCLE <= 0;
			ICYCLE <= 0;
			ICYCLE_PREV <= 0;
		end
		else if (EN && CPU_PH2) begin
			if (GRANT && !STRETCH && !EXTRA && !BREAK) begin
				NCYCLE <= ~nMREQ & ~SEQ;
				SCYCLE <= ~nMREQ &  SEQ;
				ICYCLE <=  nMREQ & ~SEQ;
				ICYCLE_PREV <= ICYCLE;
			end
		end
	end 
	assign ACCESS = ~(STRETCH | EXTRA | BREAK | ICYCLE);

	//SEQ comes from the CPU decode and only changes when the CPU ends a cycle, at the end of PHASE2.
	//START is then masked for at least two clocks (PHASE2 low, or CPU_GRANT low when a DMA keeps
	//PHASE2), so a copy delayed by one clock gives the same RAS/CODE outputs and keeps the CPU decode
	//off the paths to the memory controllers (clk_mem, 10 ns).
	bit         SEQ_R;
	always @(posedge CLK) begin
		SEQ_R <= SEQ;
	end
	assign START = (STRETCH | BREAK | (ICYCLE && SEQ_R)) & PHASE2;
	
	bit         EXEC;
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			EXEC <= 0;
		end
		else if (EN && CE_R) begin
			if (PHASE1) begin
				EXEC <= 0;
				if (GRANT && !STRETCH && !EXTRA && !BREAK && !SLOW_WAIT) EXEC <= 1;
			end
		end
	end 
	assign READY = EXEC & ~LOCK & PHASE2;
	
	wire DMA_REG_SEL = (A >= 32'h03300400 && A <= 32'h033005FF);
	always_comb begin
		AG_CTL = '0;
		if (STRETCH || EXTRA || BREAK || ICYCLE) begin
			AG_CTL.DMA_GROUP_ADDR_SEL = 0;
			AG_CTL.DMA_GROUP_HOLD = 1;
			AG_CTL.DMA_REG_READ_SEL = 0;
			AG_CTL.DMA_REG_WRITE_SEL = 0;
			AG_CTL.DMA_REG_WRITE_EN = 0;
			AG_CTL.DMA_ADDER_CTL = 4'b0000;
			AG_CTL.CPU_ADDR_SEL = 1;
			AG_CTL.DMA_ADDR_SEL = 0;
		end
		else begin
			case (PHASE2)
				1'b0: begin
					AG_CTL.DMA_GROUP_ADDR_SEL = 0;
					AG_CTL.DMA_GROUP_HOLD = 1;
					AG_CTL.DMA_REG_READ_SEL = 0;
					AG_CTL.DMA_REG_WRITE_SEL = 0;
					AG_CTL.DMA_REG_WRITE_EN = 0;
					AG_CTL.DMA_ADDER_CTL = 4'b0000;
					AG_CTL.CPU_ADDR_SEL = (ICYCLE_PREV & SCYCLE);
					AG_CTL.DMA_ADDR_SEL = DMA_REG_SEL&~nRW_FF;
				end
				1'b1: begin
					AG_CTL.DMA_GROUP_ADDR_SEL = 0;
					AG_CTL.DMA_GROUP_HOLD = 0;
					AG_CTL.DMA_REG_READ_SEL = 0;
					AG_CTL.DMA_REG_WRITE_SEL = 1;
					AG_CTL.DMA_REG_WRITE_EN = DMA_REG_SEL&nRW_FF;
					AG_CTL.DMA_ADDER_CTL = {3'b000,(NCYCLE|SCYCLE)};
					AG_CTL.CPU_ADDR_SEL = (ICYCLE_PREV & SCYCLE);
					AG_CTL.DMA_ADDR_SEL = 0;
				end
			endcase
		end
	end
	
	bit  [31: 0] A_FF;
	bit          nRW_FF,nWB_FF;
	always @(posedge CLK ) begin
		if (PHASE1 && CE_R) begin
			if (!ACCESS) A_FF <= A;
			nRW_FF <= nRW;
			nWB_FF <= nWB;
		end
	end
	assign WE = ({~A_FF[1]&~A_FF[0],~A_FF[1]&A_FF[0],A_FF[1]&~A_FF[0],A_FF[1]&A_FF[0]} | {4{nWB_FF}}) & {4{~STRETCH&~EXTRA&~BREAK&nRW_FF}};
	
	assign MCLK_PH1 = CPU_PH1 && GRANT && !STRETCH && !EXTRA && !BREAK && !PAUSE;
	assign MCLK_PH2 = CPU_PH2 && GRANT && !STRETCH && !EXTRA && !BREAK && !PAUSE;
	assign DBE = GRANT;
	
	always @(posedge CLK or negedge RST_N) begin
		if (!RST_N) begin
			MCLK <= 0;
		end
		else if (EN && CE_R) begin
			if (MCLK_PH1) begin
				MCLK <= 1;
			end
			if (MCLK_PH2) begin
				MCLK <= 0;
			end
		end
	end
	
endmodule
