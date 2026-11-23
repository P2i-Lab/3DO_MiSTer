//VRAM of the console in the two upper banks of the SDRAM (test build).
//The random accesses of the main bus go through the DRAM slots of sdram.sv. This module keeps the serial
//port. A read transfer only says which row each half of the serial register stands for; its words are read
//ahead of the read pointer, as the video needs them, and stay valid until that half row is written: the next
//line reads the other bank at the same place, and its words are already there.
//
//The CPU's copies of rows (a read transfer of the row to copy, then masked write transfers) have a register of
//their own (RB), apart from the video's serial register: in the console the copies of a frame take place in the
//vertical blank, before the video loads its next line, and the video's transfers must not come between a copy's
//read transfer and its write. RB is loaded from the cache or the SDRAM by the first masked write transfer after
//the read transfer; the video's transfers neither wait for a copy nor change it.
//
//Row cache, in the block RAM that the VRAM no longer needs: CACHE_BANKS x 32 rows of 256 pairs, direct-mapped
//(slot = row modulo the number of slots), one valid bit per block of 32 pairs. The video takes a valid block
//from it at once, so the console does not stop; a missing block is read from the SDRAM (the console stops, as
//before) and kept. A main-bus write goes into a cached valid block too (write-through); a write into the block
//being read from the SDRAM leaves that block invalid.
//
//A masked write transfer or a flash write fills the row in the cache only, in some 40 clocks, as fast as the
//console would let the CPU go on (the SDRAM would take 14 us a row, and the copies of a frame would not fit in the
//vertical blank): the row is dirty (DIRTY), and is written to the SDRAM when its slot is wanted for another row,
//or when a main-bus read wants it (sdram.sv holds that read and asks for it: WBQ). Main-bus writes into a dirty
//row go into the cache only (sdram.sv does not send them: the cel engine draws there without changing SDRAM rows),
//except into the row being written to the SDRAM (WBP, WBR), whose blocks already written must get them too.
module P3DO_VRAM #(parameter int CACHE_BANKS = 7)
(
	input          CLK_MEM,
	input          RST_N,

	input  [17: 0] A,
	input  [15: 0] D,
	input          RAS_N,
	input          CAS_N,
	input  [ 1: 0] WE_N,
	input          OE_N,
	input          DSF1,
	input          CPU_SP,		//the cycle is a serial-port access of the CPU (copy of rows, flash write), not of the video
	output reg     RDY,
	output reg     SP_BUSY,	//a masked write transfer or a flash write of the CPU is pending or running (the next VRAM cycle would wait)

	input          SC,
	input          SE_N,
	output reg [15: 0] SQ,
	output         QSF,
	output reg     SRDY,		//the serial register holds the data of the next shift
	input          DISP_DMA,	//fix_disp: MADAM asks for or runs a display DMA (display list, video transfers)
	input          CON_WAIT,	//fix_disp: the console waits for the memory
	input          DISP_BUS,	//fix_disp2: MADAM's bus is in a display DMA state (MADAM alone uses it, and reads)
	input          REF_BUS,	//fix_ref: MADAM's bus is in a memory refresh cycle

	//block port of the SDRAM controller: pairs of 16-bit words, even column first
	output reg     EREQ,
	output reg     EWE,
	output reg [17: 0] EADDR,	//row, column of the first pair
	output reg [ 8: 0] ECOUNT,	//pairs
	input          EACK,
	input          ETE,			//read: EQ holds a pair; write: the next pair is wanted
	input  [31: 0] EQ,
	output [31: 0] ED,

	output reg [511: 0] DIRTY,	//rows held in the cache only (filled or copied by the CPU), not yet written to the SDRAM
	input          WBQ,			//a main-bus read waits for this dirty row to be written to the SDRAM
	input  [ 8: 0] WBQ_ROW,
	output         WBP,			//a dirty row is being written to the SDRAM (WBR): main-bus writes into it go to the SDRAM too
	output [ 8: 0] WBR,

	//A main-bus read of a dirty row is taken from the cache (sdram.sv: CR with the row and column CR_A, the console waits):
	//the word comes in CR_Q with CR_V
	input          CR,
	input  [17: 0] CR_A,
	output reg     CR_V,
	output reg [15: 0] CR_Q
);

	localparam [7:0] LOOK_AHEAD = 8'd16;		//pairs kept loaded ahead of the read pointer (5.2 us of video)
	localparam [8:0] READ_BLOCK = 9'd32;	//pairs read at a time (one block of the cache)
	localparam [8:0] WRITE_BLOCK = 9'd32;	//pairs written at a time, the video can read between two blocks

	localparam int       C_SLOTS = CACHE_BANKS * 32;
	localparam bit [9:0] C_N1 = C_SLOTS;
	localparam bit [9:0] C_N2 = 2 * C_SLOTS;

	//cache slot of a row: the row modulo the number of slots (rows 0..511)
	function automatic [7:0] CSLOT(input [8:0] row);
		bit [9:0] r;
		r = {1'b0,row};
		if (C_SLOTS < 256 && r >= C_N2) r = r - C_N2;
		else if (C_SLOTS < 256 && r >= C_N1) r = r - C_N1;
		CSLOT = r[7:0];
	endfunction

	bit  [17: 0] A_FF;
	bit  [15: 0] D_FF;
	bit  [15: 0] COLOR;

	bit  [ 8: 0] MEM_ROW;	//row written by a masked write transfer or a flash write
	bit          MWT_EXEC;	//masked write transfer
	bit          FW_EXEC;	//flash write
	bit          SOM;			//serial output mode
	bit  [ 8: 0] SR_RA;

	//What each half of the serial register stands for (LG_) and what it holds: the pairs PH_FROM to PH_TO - 1 of a half row
	bit  [ 8: 0] LG_ROW [2];
	bit  [ 1: 0] LG_SET;
	bit  [ 8: 0] PH_ROW [2];
	bit  [ 1: 0] PH_VALID;
	bit  [ 6: 0] PH_FROM [2];
	bit  [ 7: 0] PH_TO [2];		//up to 128

	//The CPU's copy register: the row of its last read transfer, and the blocks of 32 pairs of that row RB holds
	bit  [ 8: 0] RB_ROW;
	bit  [ 7: 0] RB_VAL;
	bit          RB_REQ;		//a block of RB is on its way (from the cache or the SDRAM)
	wire         RB_FULL = &RB_VAL;

	//Write of a dirty row to the SDRAM: its pairs WR_POS on, from the cache, a chunk of 8 pairs at a time (WB_BUF; the
	//next chunk is read ahead into WB_NEXT)
	bit          WB_PEND;
	bit  [ 8: 0] WB_ROW;
	bit  [ 7: 0] WB_SLOT;
	bit  [ 8: 1] WR_POS;
	bit  [ 2: 0] WB_RDCNT;	//clocks from the start of a block to the read of its first chunk (the last main-bus write is in)
	bit          WB_RD1,WB_RD2,WB_NX1,WB_NX2;
	bit [255: 0] WB_BUF,WB_NEXT;
	bit          V_BLOCKED;	//the video waits for the write of the dirty row in the slot it wants
	bit          WBQ_OK;		//WBQ a clock later, for a row still dirty
	bit  [ 8: 0] WBQ_RFF;

	bit          BLOCK_BUSY;
	bit          BLOCK_WE;
	bit          FILL_H;
	bit  [ 6: 0] FILL_Q;
	bit          FILL_RB;		//the block read goes into RB, not into the serial register
	bit          FILL_NC;		//the block read does not go into the cache (its slot holds another, dirty row)
	bit  [ 8: 0] BLOCK_CNT;
	bit          REQ_H;
	bit  [ 6: 0] REQ_Q;
	bit          REQ_RB;
	bit          REQ_NC;
	bit          DH = 0;		//fix_disp: no block that stops the console, apart from the video's (see C_IDLE)
	wire         DH_ON = 1'b1;
	wire         DR_ON = 1'b1;
	//fix_disp2: a read of MADAM's display DMA that does not depend on the masked write transfer in progress (not its rows, not the
	//cache slot it writes) does not wait for it
	wire         DR_OK = DR_ON && DISP_BUS && MWT_EXEC && !FW_EXEC && OE_N && WE_N && !DSF1 &&
	                     A[17:9] != MEM_ROW && A[17:9] != RB_ROW && CSLOT(A[17:9]) != CSLOT(MEM_ROW);
	wire         RF_ON = 1'b1;
	//fix_ref: a memory refresh cycle of MADAM (no CAS, no transfer: nothing happens here for it) does not wait for a masked write
	//transfer or a flash write
	wire         RF_OK = RF_ON && REF_BUS && OE_N && WE_N && !DSF1;

	bit  [ 8: 1] SR_WP;
	bit  [31: 0] SR_WD;
	bit          SR_WE;
	bit [255: 0] SR_Q;

	bit  [ 8: 1] RB_WP;
	bit  [31: 0] RB_WD;
	bit          RB_WE;
	bit          RB_CW;		//a whole chunk from the cache goes into RB
	bit  [ 4: 0] RB_CWA;
	bit [255: 0] RB_CWD;
	bit [255: 0] RB_Q;

	//names the traces of the simulator look at
	bit          RT_EXEC;
	bit          SPLIT;
	wire         ESTE = ETE;
	wire         ISTE = 1'b0;
	wire [ 8: 0] SREG_POS = {SR_WP,1'b0};

	wire         P_H = SR_RA[8];
	wire [ 6: 0] P_Q = SR_RA[7:1];
	wire         N_H = ~P_H;
	wire         TAG_P = PH_VALID[P_H] && PH_ROW[P_H] == LG_ROW[P_H];
	wire         TAG_N = PH_VALID[N_H] && PH_ROW[N_H] == LG_ROW[N_H];
	wire         HIT_P = TAG_P && P_Q >= PH_FROM[P_H] && {1'b0,P_Q} < PH_TO[P_H];
	wire [ 7: 0] P_END = {1'b0,P_Q} + LOOK_AHEAD;
	//Is the word after SR_RA loaded? SRDY comes a clock late and MADAM can shift the colour words of the VDL two clk_sys apart:
	//in the clock a shift moves SR_RA, SRDY tells about the word the next shift gives
	wire [ 7: 0] NX_Q = {1'b0,P_Q} + 8'd1;
	wire         HIT_NX = !SR_RA[0] ? HIT_P :
	                      !NX_Q[7] ? (TAG_P && NX_Q[6:0] >= PH_FROM[P_H] && NX_Q < PH_TO[P_H]) :
	                      (TAG_N && PH_FROM[N_H] == 7'd0 && PH_TO[N_H] != 8'd0);

	//Which pairs of the video to read next
	bit          WANT;
	bit          WANT_H;
	bit  [ 6: 0] WANT_Q;
	always_comb begin
		WANT = 0;
		WANT_H = P_H;
		WANT_Q = P_Q;
		if (SOM && LG_SET[P_H]) begin
			if (!HIT_P) begin
				WANT = 1;
			end else if (PH_TO[P_H] <= P_END) begin
				if (PH_TO[P_H] < 8'd128) begin
					WANT = 1;
					WANT_Q = PH_TO[P_H][6:0];
				end else if (LG_SET[N_H] && !(TAG_N && PH_FROM[N_H] == 7'd0 && {1'b0,PH_TO[N_H]} + 9'd128 > {1'b0,P_END})) begin
					WANT = 1;
					WANT_H = N_H;
					WANT_Q = TAG_N && PH_FROM[N_H] == 7'd0 ? PH_TO[N_H][6:0] : 7'd0;
				end
			end
		end
	end

	//The next block of RB a masked write transfer waits for
	bit  [ 2: 0] RB_NEXT;
	always_comb begin
		RB_NEXT = 3'd0;
		for (int i = 7; i >= 0; i--) if (!RB_VAL[i]) RB_NEXT = i[2:0];
	end
	wire         WANT_RB = MWT_EXEC && !RB_FULL && !RB_REQ;

	//RB full a clock later (MWT_WB): the masked write transfer can fill its row
	bit          MWT_WB;
	wire         DO_FILL = FW_EXEC || (MWT_EXEC && MWT_WB);

	//Cache tags: {dirty, valid bit of each block of 32 pairs, row}, in two copies (requests, main-bus writes)
	bit  [17: 0] TAG_A [256];
	bit  [17: 0] TAG_B [256];
	bit          TW_EN;
	bit  [ 7: 0] TW_SLOT;
	bit  [17: 0] TW_DATA;
	bit  [ 7: 0] TA_SLOT,TB_SLOT;
	bit  [17: 0] TA_Q,TB_Q;
	always @(posedge CLK_MEM) begin
		if (TW_EN) begin
			TAG_A[TW_SLOT] <= TW_DATA;
			TAG_B[TW_SLOT] <= TW_DATA;
		end
		TA_Q <= TAG_A[TA_SLOT];
		TB_Q <= TAG_B[TB_SLOT];
	end

	//Cache data: port A takes the 16-bit writes of the main bus, port B chunks of 8 pairs laid out as in the
	//serial register (pair p in bits 32p+31..32p, even column in the upper half)
	bit  [ 2: 0] CA_BANK;
	bit  [13: 0] CA_ADDR;
	bit  [15: 0] CA_D;
	bit  [ 1: 0] CA_BE;
	bit          CA_WE;
	bit  [ 2: 0] CB_BANK;
	bit  [ 9: 0] CB_ADDR;
	bit [255: 0] CB_D;
	bit          CB_WE;
	bit  [ 2: 0] CB_RBANK;
	wire[255: 0] CB_Q [CACHE_BANKS];
	wire [15: 0] CA_Q [CACHE_BANKS];
	genvar gb;
	generate
		for (gb = 0; gb < CACHE_BANKS; gb++) begin : cbank
			VRAM_MEM #(14) mem
			(
				.CLK(CLK_MEM),
				.ADDR(CA_ADDR),
				.DATA(CA_D),
				.WREN(CA_WE && CA_BANK == gb ? CA_BE : 2'b00),
				.Q(CA_Q[gb]),

				.SCLK(CLK_MEM),
				.SADDR(CB_ADDR),
				.SDATA(CB_D),
				.SWREN(CB_WE && CB_BANK == gb),
				.SQ(CB_Q[gb])
			);
		end
	endgenerate
	wire [255: 0] CB_RQ = CB_Q[CB_RBANK];

	//Main-bus writes: taken from the bus a clock later (MW_ON_FF, A_FF, D_FF, WE_N_FF: the bus signals arrive late
	//in the clock), then two stages (tag read, cache write). A serial-port cycle of the CPU (CPU_SP: masked write transfer,
	//flash write) holds WE low from its RAS, with the row in A and column 0: it writes no word (on the VRAM chips WE low at
	//RAS chooses the transfer; 1114i only takes a WE that falls while RAS is already low), so it is not a main-bus write
	//(vsd43 wrote the cycle's data word into column 0 of the row in the cache)
	wire         MW_ON = !RAS_N && WE_N != 2'b11 && !CPU_SP;
	bit          MW_ON_FF;
	bit  [ 1: 0] WE_N_FF;
	bit          MW1_V,MW2_V;
	bit  [ 8: 0] MW1_ROW,MW2_ROW;
	bit  [ 8: 0] MW1_COL,MW2_COL;
	bit  [15: 0] MW1_D,MW2_D;
	bit  [ 1: 0] MW1_BE,MW2_BE;
	bit  [ 7: 0] MW2_SLOT;
	wire [ 7: 0] TB_VAL = TB_Q[16:9];
	wire         MW2_HIT = MW2_V && TB_Q[8:0] == MW2_ROW && TB_VAL[MW2_COL[8:6]];

	//Main-bus reads of dirty rows, from port A of the cache when no write takes it and no older write to the same word is
	//still on its way (a dirty row is whole in its slot); the word is there two clocks after its address
	bit          CR_PEND;
	bit  [17: 0] CR_ADDR;
	bit          CR_RD1,CR_RD2;
	bit  [ 2: 0] CR_BANK1,CR_BANK2;
	wire [17: 0] CR_NA = CR_PEND ? CR_ADDR : CR_A;
	wire         CR_HAZ = (MW_ON_FF && A_FF == CR_NA) || (MW1_V && {MW1_ROW,MW1_COL} == CR_NA) || (MW2_V && {MW2_ROW,MW2_COL} == CR_NA);
	wire [ 7: 0] CR_SLOT = CSLOT(CR_NA[17:9]);
	wire         CR_GO = (CR_PEND || CR) && !CR_HAZ && !MW2_HIT;

	//Requests of the video, of the masked write transfers and flash writes, writes of dirty rows
	typedef enum bit [3:0] {C_INIT, C_IDLE, C_LOOK, C_LOOK2, C_HOLD, C_COPY, C_DRAIN, C_DLOOK, C_DLOOK2, C_DFILL} cstate_t;
	cstate_t     C_ST;
	bit  [ 7: 0] C_INIT_N;
	bit  [ 1: 0] C_HOLD_N;
	bit  [ 8: 0] CQ_ROW;
	bit          CQ_H;
	bit  [ 6: 0] CQ_Q;
	bit  [ 7: 0] CQ_SLOT;
	bit          CQ_RB;		//the request loads RB (a masked write transfer), not the serial register (the video)
	wire [ 7: 0] TA_VAL = TA_Q[16:9];
	wire         TA_DIRTY = TA_Q[17];
	wire         CQ_HIT = TA_Q[8:0] == CQ_ROW && TA_VAL[{CQ_H,CQ_Q[6:5]}];
	//A main-bus write to the requested half row taken in the very clock the copy starts would not make the serial
	//register out of date (its row is set at that clock): try again. The writes taken before are still on their
	//way into the cache: the copy waits 3 clocks (C_HOLD) for them; later ones make the serial register out of
	//date as usual.
	wire         CQ_WAIT = MW_ON_FF && A_FF[17:8] == {CQ_ROW,CQ_H};

	//copy of a cached block into the serial register (or RB): chunk addresses, then the data two clocks later
	bit          CP_H;
	bit          CP_RB;
	bit  [ 3: 0] CP_CH;		//next chunk of the half to read
	bit  [ 3: 0] CP_LAST;	//last chunk of the block
	bit          CP_V1,CP_V2;
	bit  [ 3: 0] CP_CH1,CP_CH2;
	bit          SR_CW;		//a whole chunk goes into the serial register
	bit  [ 4: 0] SR_CWA;
	bit [255: 0] SR_CWD;

	//fill of a row of the cache by a masked write transfer (from RB) or a flash write (the colour)
	bit  [ 7: 0] DF_SLOT;
	bit  [ 4: 0] DF_CH;
	bit          DF_FW;

	//block read from the SDRAM into the cache
	bit          F_PEND;		//from the request to the end of the block
	bit          F_DIRTY;		//a main-bus write went into the block meanwhile
	bit  [ 7: 0] F_SLOT;
	bit  [ 8: 0] F_ROW;
	bit  [ 2: 0] F_BLK;
	bit  [ 7: 0] F_VAL;
	wire         MW_FILL = MW_ON_FF && A_FF[17:9] == F_ROW && A_FF[8:6] == F_BLK;
	bit  [31: 0] CBUF [7];	//pairs 0 to 6 of the chunk being gathered

	always @(posedge CLK_MEM or negedge RST_N) begin
		bit        RAS_N_OLD,CAS_N_OLD,SC_OLD;
		bit        RDY_OLD;
		bit        RT_PEND,MWT_PEND,FW_PEND,FW_PEND2,FW_OE_N;
		bit        RT_CPU;
		bit        FLASHWRITE,LOAD;

		if (!RST_N) begin
			{RT_PEND,MWT_PEND,FW_PEND,FW_PEND2} <= '0;
			RT_CPU <= 0;
			RDY <= 1;
			SP_BUSY <= 0;
			DH <= 0;
			MEM_ROW <= '0;
			{FLASHWRITE,LOAD} <= '0;
			{MWT_EXEC,FW_EXEC} <= '0;
			SOM <= 0;
			WR_POS <= '0;
			SR_WP <= '0;
			SR_WE <= 0;
			SR_RA <= '0;
			SRDY <= 0;
			LG_SET <= '0;
			PH_VALID <= '0;
			RB_ROW <= '0;
			RB_VAL <= '0;
			RB_REQ <= 0;
			RB_WE <= 0;
			RB_CW <= 0;
			WB_PEND <= 0;
			WB_RDCNT <= '0;
			{WB_RD1,WB_RD2,WB_NX1,WB_NX2} <= '0;
			V_BLOCKED <= 0;
			WBQ_OK <= 0;
			DIRTY <= '0;
			BLOCK_BUSY <= 0;
			EREQ <= 0;
			RT_EXEC <= 0;
			SPLIT <= 0;
			C_ST <= C_INIT;
			C_INIT_N <= '0;
			TW_EN <= 0;
			CA_WE <= 0;
			{CR_PEND,CR_RD1,CR_RD2,CR_V} <= '0;
			CB_WE <= 0;
			SR_CW <= 0;
			{CP_V1,CP_V2} <= '0;
			{MW1_V,MW2_V} <= '0;
			MW_ON_FF <= 0;
			MWT_WB <= 0;
			{F_PEND,F_DIRTY} <= '0;
		end else begin
			SRDY <= SOM && RDY && RDY_OLD && !RT_PEND && !MWT_PEND && (!SC && SC_OLD ? HIT_NX : HIT_P);
			RT_EXEC <= RT_PEND;
			SP_BUSY <= MWT_PEND | MWT_EXEC | FW_PEND | FLASHWRITE | FW_EXEC;
			DH <= DH_ON && DISP_DMA && !CON_WAIT;

			A_FF <= A;
			D_FF <= D;
			MW_ON_FF <= MW_ON;
			WE_N_FF <= WE_N;
			MWT_WB <= MWT_EXEC && RB_FULL;
			WBQ_OK <= WBQ && DIRTY[WBQ_ROW];
			WBQ_RFF <= WBQ_ROW;
			RAS_N_OLD <= RAS_N;
			CAS_N_OLD <= CAS_N;
			RDY_OLD <= RDY;
			if ((!RAS_N && RAS_N_OLD) || (RDY && !RDY_OLD)) begin
				if (!OE_N && WE_N && !CPU_SP) begin
					//a read transfer of the video: its own register, it does not wait for a copy
					RT_PEND <= 1;
					RT_CPU <= 0;
				end else if ((MWT_EXEC || FW_EXEC) && !DR_OK && !RF_OK) begin
					RDY <= 0;
				end else if (!OE_N && WE_N) begin
					RT_PEND <= 1;
					RT_CPU <= 1;
				end else if (!OE_N && !WE_N) begin
					MWT_PEND <= 1;
				end else if (OE_N && !WE_N) begin
					FW_PEND <= 1;
				end else if (OE_N && WE_N) begin
					if (DSF1) LOAD <= 1;
				end
			end
			else if (!RDY && !MWT_EXEC && !FW_EXEC) begin
				RDY <= 1;
			end
			if (!CAS_N && CAS_N_OLD) begin
				FW_PEND2 <= 1;
				FW_OE_N <= OE_N;
			end

			if (RT_PEND) begin
				RT_PEND <= 0;
				if (RT_CPU) begin
					//the CPU's read transfer: the row its masked write transfers copy, loaded into RB by the first of them
					RB_ROW <= A_FF[17:9];
					RB_VAL <= '0;
				end else begin
					SOM <= 1;
					SPLIT <= DSF1;
					if (DSF1) begin
						LG_ROW[A_FF[8]] <= A_FF[17:9];
						LG_SET[A_FF[8]] <= 1;
					end else begin
						LG_ROW[0] <= A_FF[17:9];
						LG_ROW[1] <= A_FF[17:9];
						LG_SET <= '1;
						SR_RA <= A_FF[8:0];
					end
				end
			end else if (MWT_PEND) begin
				MWT_PEND <= 0;
				MEM_ROW <= A_FF[17:9];
				MWT_EXEC <= !SE_N;
			end else if (FW_PEND) begin
				FW_PEND <= 0;
				MEM_ROW <= A_FF[17:9];
				FLASHWRITE <= DSF1;
			end
			if (FW_PEND2) begin
				FW_PEND2 <= 0;
				if (FLASHWRITE) begin
					FW_EXEC <= 1;
				end
				if (FW_OE_N) begin
					if (LOAD && DSF1) COLOR <= D_FF;
				end
				FLASHWRITE <= 0;
				LOAD <= 0;
			end

			SC_OLD <= SC;
			if (!SC && SC_OLD && SOM) begin
				SR_RA <= SR_RA + 9'h1;
				case (SR_RA[0])
					1'b0: SQ <= SR_DATA[31:16];
					1'b1: SQ <= SR_DATA[15: 0];
				endcase
			end

			//A write from the main bus makes the words of this half row out of date in the serial register
			if (MW_ON_FF) begin
				if (PH_ROW[A_FF[8]] == A_FF[17:9]) PH_VALID[A_FF[8]] <= 0;
			end

			//and goes into the cache when its block is there
			MW1_V <= MW_ON_FF;
			MW1_ROW <= A_FF[17:9];
			MW1_COL <= A_FF[8:0];
			MW1_D <= D_FF;
			MW1_BE <= ~WE_N_FF;
			TB_SLOT <= CSLOT(A_FF[17:9]);
			MW2_V <= MW1_V;
			MW2_ROW <= MW1_ROW;
			MW2_COL <= MW1_COL;
			MW2_D <= MW1_D;
			MW2_BE <= MW1_BE;
			MW2_SLOT <= TB_SLOT;
			CA_WE <= MW2_HIT;
			CA_D <= MW2_D;
			CA_BE <= MW2_BE;
			//(the address of port A is chosen by MW2_HIT alone: without a write (CA_WE) and without a read of a dirty row (CR_GO,
			//CR_RD1) the port does nothing that is used, so CR_GO, which comes late from sdram.sv, need not choose it)
			if (MW2_HIT) begin
				CA_BANK <= MW2_SLOT[7:5];
				CA_ADDR <= {MW2_SLOT[4:0],MW2_COL[8:1],~MW2_COL[0]};
			end else begin
				//port A reads the word of a main-bus read of a dirty row
				CA_BANK <= CR_SLOT[7:5];
				CA_ADDR <= {CR_SLOT[4:0],CR_NA[8:1],~CR_NA[0]};
			end
			if (CR && !CR_GO) begin
				CR_PEND <= 1;
				CR_ADDR <= CR_A;
			end
			if (CR_GO) CR_PEND <= 0;
			CR_RD1 <= CR_GO;
			CR_BANK1 <= CR_SLOT[7:5];
			CR_RD2 <= CR_RD1;
			CR_BANK2 <= CR_BANK1;
			CR_V <= CR_RD2;
			if (CR_RD2) CR_Q <= CA_Q[CR_BANK2];
			if (F_PEND && MW_FILL) F_DIRTY <= 1;

			TW_EN <= 0;
			CB_WE <= 0;
			SR_WE <= 0;
			SR_CW <= 0;
			RB_WE <= 0;
			RB_CW <= 0;

			//chunks read from the cache land in the serial register or in RB
			CP_V2 <= CP_V1;
			CP_CH2 <= CP_CH1;
			CP_V1 <= 0;
			if (CP_V2) begin
				if (CP_RB) begin
					RB_CW <= 1;
					RB_CWA <= {CP_H,CP_CH2};
					RB_CWD <= CB_RQ;
				end else begin
					SR_CW <= 1;
					SR_CWA <= {CP_H,CP_CH2};
					SR_CWD <= CB_RQ;
				end
			end
			if (SR_CW) PH_TO[CP_H] <= PH_TO[CP_H] + 8'd8;
			if (RB_CW && RB_CWA[1:0] == 2'b11) begin
				RB_VAL[RB_CWA[4:2]] <= 1;
				RB_REQ <= 0;
			end

			//chunks of a dirty row read for its write to the SDRAM
			WB_RD2 <= WB_RD1;
			WB_NX2 <= WB_NX1;
			WB_RD1 <= 0;
			if (WB_RD2) begin
				if (WB_NX2) WB_NEXT <= CB_RQ;
				else        WB_BUF <= CB_RQ;
			end
			if (WB_RDCNT != 3'd0) begin
				WB_RDCNT <= WB_RDCNT - 3'd1;
				if (WB_RDCNT == 3'd1) begin
					//the first chunk of the block, read again now that the console is stopped and its last write is in
					CB_ADDR <= {WB_SLOT[4:0],WR_POS[8:4]};
					CB_RBANK <= WB_SLOT[7:5];
					WB_RD1 <= 1;
					WB_NX1 <= 0;
				end
			end

			case (C_ST)
				C_INIT: begin
					TW_EN <= 1;
					TW_SLOT <= C_INIT_N;
					TW_DATA <= '0;
					C_INIT_N <= C_INIT_N + 8'd1;
					if (C_INIT_N == 8'hFF) C_ST <= C_IDLE;
				end

				C_IDLE: if (!EREQ && !BLOCK_BUSY && !EACK && !SR_CW && !CP_V2 && !RB_CW) begin
					if (WANT && !V_BLOCKED) begin
						//the video first: the display does not wait
						CQ_RB <= 0;
						CQ_ROW <= LG_ROW[WANT_H];
						CQ_H <= WANT_H;
						CQ_Q <= WANT_Q;
						CQ_SLOT <= CSLOT(LG_ROW[WANT_H]);
						TA_SLOT <= CSLOT(LG_ROW[WANT_H]);
						C_ST <= C_LOOK;
					end else if (DH && !V_BLOCKED) begin
						//fix_disp: MADAM asks for or runs a display DMA and the console does not wait for the memory: no copy
						//register fill, cache fill or dirty-row write now (an SDRAM block stops the console, and the display list
						//of the next lines would be read late: picture one line off, colour words lost)
					end else if (WB_PEND) begin
						//the next block of the dirty row being written to the SDRAM
						EREQ <= 1;
						EWE <= 1;
						EADDR <= {WB_ROW,WR_POS,1'b0};
						ECOUNT <= WRITE_BLOCK;
					end else if (WBQ_OK) begin
						//a main-bus read waits for this row
						WB_PEND <= 1;
						WB_ROW <= WBQ_RFF;
						WB_SLOT <= CSLOT(WBQ_RFF);
						WR_POS <= '0;
					end else if (DO_FILL) begin
						//a masked write transfer or a flash write: its row in the cache, once the slot is free
						TA_SLOT <= CSLOT(MEM_ROW);
						C_ST <= C_DLOOK;
					end else if (WANT_RB) begin
						//the next block of the row a masked write transfer copies
						CQ_RB <= 1;
						CQ_ROW <= RB_ROW;
						CQ_H <= RB_NEXT[2];
						CQ_Q <= {RB_NEXT[1:0],5'b00000};
						CQ_SLOT <= CSLOT(RB_ROW);
						TA_SLOT <= CSLOT(RB_ROW);
						RB_REQ <= 1;
						C_ST <= C_LOOK;
					end
				end

				C_LOOK: C_ST <= C_LOOK2;	//the tag RAM reads the slot

				C_LOOK2: begin
					if (CQ_HIT && !CQ_WAIT) begin
						//the block is in the cache: copy it into the serial register (from the chunk of the wanted
						//pair) or into RB
						if (!CQ_RB && !(PH_VALID[CQ_H] && PH_ROW[CQ_H] == CQ_ROW && PH_TO[CQ_H] == {1'b0,CQ_Q[6:3],3'b000})) begin
							PH_ROW[CQ_H] <= CQ_ROW;
							PH_VALID[CQ_H] <= 1;
							PH_FROM[CQ_H] <= {CQ_Q[6:3],3'b000};
							PH_TO[CQ_H] <= {1'b0,CQ_Q[6:3],3'b000};
						end
						CP_RB <= CQ_RB;
						CP_H <= CQ_H;
						CP_CH <= CQ_Q[6:3];
						//(RB takes the whole half row at once when the cache holds all of it)
						if (CQ_RB && CQ_Q[6:5] == 2'b00 && (CQ_H ? &TA_VAL[7:4] : &TA_VAL[3:0])) CP_LAST <= 4'hF;
						else CP_LAST <= {CQ_Q[6:5],2'b11};
						CB_RBANK <= CQ_SLOT[7:5];
						C_HOLD_N <= 2'd2;
						C_ST <= C_HOLD;
					end else if (CQ_HIT) begin
						if (CQ_RB) RB_REQ <= 0;
						C_ST <= C_IDLE;	//try again once the write is in
					end else if (TA_DIRTY && TA_Q[8:0] != CQ_ROW) begin
						//the slot holds another, dirty row: read the block from the SDRAM without taking the slot, so that
						//the display (or the copy) does not wait for the write of a whole row; the dirty row stays until a
						//masked write transfer, a flash write or a main-bus read needs it written
						EREQ <= 1;
						EWE <= 0;
						EADDR <= {CQ_ROW,CQ_H,CQ_Q[6:5],5'b00000,1'b0};
						ECOUNT <= READ_BLOCK;
						REQ_H <= CQ_H;
						REQ_Q <= {CQ_Q[6:5],5'b00000};
						REQ_RB <= CQ_RB;
						REQ_NC <= 1;
						C_ST <= C_IDLE;
					end else begin
						//not in the cache: the SDRAM block port reads the whole block, which the cache keeps
						EREQ <= 1;
						EWE <= 0;
						EADDR <= {CQ_ROW,CQ_H,CQ_Q[6:5],5'b00000,1'b0};
						ECOUNT <= READ_BLOCK;
						REQ_H <= CQ_H;
						REQ_Q <= {CQ_Q[6:5],5'b00000};
						REQ_RB <= CQ_RB;
						REQ_NC <= 0;
						F_PEND <= 1;
						F_DIRTY <= 0;
						F_SLOT <= CQ_SLOT;
						F_ROW <= CQ_ROW;
						F_BLK <= {CQ_H,CQ_Q[6:5]};
						if (TA_Q[8:0] == CQ_ROW) begin
							F_VAL <= TA_VAL;
						end else begin
							F_VAL <= 8'h00;
							TW_EN <= 1;
							TW_SLOT <= CQ_SLOT;
							TW_DATA <= {1'b0,8'h00,CQ_ROW};
						end
						C_ST <= C_IDLE;
					end
				end

				C_HOLD: begin
					C_HOLD_N <= C_HOLD_N - 2'd1;
					if (C_HOLD_N == 2'd0) C_ST <= C_COPY;
				end

				C_COPY: begin
					CB_ADDR <= {CQ_SLOT[4:0],CP_H,CP_CH};
					CP_V1 <= 1;
					CP_CH1 <= CP_CH;
					CP_CH <= CP_CH + 4'd1;
					if (CP_CH == CP_LAST) C_ST <= C_DRAIN;
				end

				C_DRAIN: if (!CP_V1 && !CP_V2) C_ST <= C_IDLE;

				C_DLOOK: C_ST <= C_DLOOK2;	//the tag RAM reads the slot

				C_DLOOK2: begin
					if (TA_DIRTY && TA_Q[8:0] != MEM_ROW) begin
						//the slot holds another dirty row: write it to the SDRAM first, then try again
						if (!WB_PEND) begin
							WB_PEND <= 1;
							WB_ROW <= TA_Q[8:0];
							WB_SLOT <= TA_SLOT;
							WR_POS <= '0;
						end
						C_ST <= C_IDLE;
					end else begin
						//the slot is the row's, invalid until it is filled
						DF_SLOT <= TA_SLOT;
						DF_CH <= '0;
						DF_FW <= FW_EXEC;
						TW_EN <= 1;
						TW_SLOT <= TA_SLOT;
						TW_DATA <= {1'b0,8'h00,MEM_ROW};
						C_ST <= C_DFILL;
					end
				end

				C_DFILL: begin
					CB_WE <= 1;
					CB_BANK <= DF_SLOT[7:5];
					CB_ADDR <= {DF_SLOT[4:0],DF_CH};
					CB_D <= DF_FW ? {16{COLOR}} : RB_Q;
					DF_CH <= DF_CH + 5'd1;
					if (DF_CH == 5'd31) begin
						//done: the row is dirty, the console goes on
						TW_EN <= 1;
						TW_SLOT <= DF_SLOT;
						TW_DATA <= {1'b1,8'hFF,MEM_ROW};
						DIRTY[MEM_ROW] <= 1;
						MWT_EXEC <= 0;
						FW_EXEC <= 0;
						if (PH_ROW[0] == MEM_ROW) PH_VALID[0] <= 0;
						if (PH_ROW[1] == MEM_ROW) PH_VALID[1] <= 0;
						C_ST <= C_IDLE;
					end
				end

				default: C_ST <= C_IDLE;
			endcase

			if (EACK) begin
				EREQ <= 0;
				BLOCK_BUSY <= 1;
				BLOCK_WE <= EWE;
				BLOCK_CNT <= ECOUNT;
				if (EWE) begin
					WB_RDCNT <= 3'd4;
				end else begin
					FILL_H <= REQ_H;
					FILL_Q <= REQ_Q;
					FILL_RB <= REQ_RB;
					FILL_NC <= REQ_NC;
					if (!REQ_RB && !(PH_VALID[REQ_H] && PH_ROW[REQ_H] == EADDR[17:9] && {1'b0,REQ_Q} == PH_TO[REQ_H])) begin
						PH_ROW[REQ_H] <= EADDR[17:9];
						PH_VALID[REQ_H] <= 1;
						PH_FROM[REQ_H] <= REQ_Q;
						PH_TO[REQ_H] <= {1'b0,REQ_Q};
					end
				end
			end
			if (ETE) begin
				BLOCK_CNT <= BLOCK_CNT - 9'd1;
				if (BLOCK_CNT == 9'd1) BLOCK_BUSY <= 0;
				if (BLOCK_WE) begin
					//a pair of the dirty row went to the SDRAM; the next chunk is read ahead
					WR_POS <= WR_POS + 8'd1;
					if (WR_POS[3:1] == 3'd4 && WR_POS[8:4] != 5'h1F) begin
						CB_ADDR <= {WB_SLOT[4:0],WR_POS[8:4] + 5'd1};
						CB_RBANK <= WB_SLOT[7:5];
						WB_RD1 <= 1;
						WB_NX1 <= 1;
					end
					if (WR_POS[3:1] == 3'd7) WB_BUF <= WB_NEXT;
					if (WR_POS == 8'hFF) begin
						//the whole row is in the SDRAM: the slot keeps it, clean
						WB_PEND <= 0;
						V_BLOCKED <= 0;
						WBQ_OK <= 0;
						DIRTY[WB_ROW] <= 0;
						TW_EN <= 1;
						TW_SLOT <= WB_SLOT;
						TW_DATA <= {1'b0,8'hFF,WB_ROW};
					end
				end else begin
					if (FILL_RB) begin
						RB_WP <= {FILL_H,FILL_Q};
						RB_WD <= EQ;
						RB_WE <= 1;
					end else begin
						SR_WP <= {FILL_H,FILL_Q};
						SR_WD <= EQ;
						SR_WE <= 1;
						PH_TO[FILL_H] <= PH_TO[FILL_H] + 8'd1;
					end
					FILL_Q <= FILL_Q + 7'd1;
					//and into the cache, a chunk of 8 pairs at a time
					CBUF[FILL_Q[2:0]] <= EQ;
					if (FILL_Q[2:0] == 3'd7 && !FILL_NC) begin
						CB_WE <= 1;
						CB_BANK <= F_SLOT[7:5];
						CB_ADDR <= {F_SLOT[4:0],FILL_H,FILL_Q[6:3]};
						CB_D <= {EQ,CBUF[6],CBUF[5],CBUF[4],CBUF[3],CBUF[2],CBUF[1],CBUF[0]};
					end
					if (BLOCK_CNT == 9'd1) begin
						if (FILL_RB) begin
							RB_VAL[{FILL_H,FILL_Q[6:5]}] <= 1;
							RB_REQ <= 0;
						end
						if (F_PEND) begin
							F_PEND <= 0;
							if (!F_DIRTY && !MW_FILL) begin
								TW_EN <= 1;
								TW_SLOT <= F_SLOT;
								TW_DATA <= {1'b0,F_VAL | (8'h01 << F_BLK),F_ROW};
							end
						end
					end
				end
			end
		end
	end

	assign WBP = WB_PEND;
	assign WBR = WB_ROW;

	VRAM_SHIFTREG #(9) shiftreg
	(
		.CLK(CLK_MEM),
		.WADDR(SR_CW ? SR_CWA : SR_WP[8:4]),
		.DATA(SR_CW ? SR_CWD : {8{SR_WD}}),
		.WREN(SR_CW ? 8'hFF : {7'b0000000,SR_WE}<<SR_WP[3:1]),
		.RADDR(SR_RA[8:4]),
		.Q(SR_Q)
	);

	bit  [31: 0] SR_DATA;
	always_comb begin
		case (SR_RA[3:1])
			3'h0: SR_DATA = SR_Q[031:000];
			3'h1: SR_DATA = SR_Q[063:032];
			3'h2: SR_DATA = SR_Q[095:064];
			3'h3: SR_DATA = SR_Q[127:096];
			3'h4: SR_DATA = SR_Q[159:128];
			3'h5: SR_DATA = SR_Q[191:160];
			3'h6: SR_DATA = SR_Q[223:192];
			3'h7: SR_DATA = SR_Q[255:224];
		endcase
	end

	//The CPU's copy register, read a chunk at a time into the cache row of the masked write transfer
	VRAM_SHIFTREG #(9) copyreg
	(
		.CLK(CLK_MEM),
		.WADDR(RB_CW ? RB_CWA : RB_WP[8:4]),
		.DATA(RB_CW ? RB_CWD : {8{RB_WD}}),
		.WREN(RB_CW ? 8'hFF : {7'b0000000,RB_WE}<<RB_WP[3:1]),
		.RADDR(DF_CH),
		.Q(RB_Q)
	);

	//The pair of the dirty row the SDRAM writes
	bit  [31: 0] WB_DATA;
	always_comb begin
		case (WR_POS[3:1])
			3'h0: WB_DATA = WB_BUF[031:000];
			3'h1: WB_DATA = WB_BUF[063:032];
			3'h2: WB_DATA = WB_BUF[095:064];
			3'h3: WB_DATA = WB_BUF[127:096];
			3'h4: WB_DATA = WB_BUF[159:128];
			3'h5: WB_DATA = WB_BUF[191:160];
			3'h6: WB_DATA = WB_BUF[223:192];
			3'h7: WB_DATA = WB_BUF[255:224];
		endcase
	end

	assign ED = WB_DATA;
	assign QSF = SR_RA[8];

endmodule
