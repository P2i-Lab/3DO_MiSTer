module sdram
(
	inout  reg [15: 0] SDRAM_DQ,   // 16 bit bidirectional data bus
	output reg [12: 0] SDRAM_A,    // 13 bit multiplexed address bus
	output reg         SDRAM_DQML, // byte mask
	output reg         SDRAM_DQMH, // byte mask
	output reg [ 1: 0] SDRAM_BA,   // two banks
	output             SDRAM_nCS,  // a single chip select
	output reg         SDRAM_nWE,  // write enable
	output reg         SDRAM_nRAS, // row address select
	output reg         SDRAM_nCAS, // columns address select
	output             SDRAM_CLK,
	output             SDRAM_CKE,

	// cpu/chipset interface
	input              init,			// init signal after FPGA config to initialize RAM
	input              clk,			// sdram is accessed at up to 128MHz
	input              sync,			//

	input      [22: 2] laddr,
	input      [ 1: 0] lwe,
	input              lras,
	input              lvram,		//the access of the left channel is in the VRAM (bank 2, not 0)
	input      [ 3: 0] lcode,
	input      [22: 2] raddr,
	input      [ 1: 0] rwe,
	input              rras,
	input              rvram,		//the access of the right channel is in the VRAM (bank 3, not 1)
	input      [ 3: 0] rcode,

	input      [31: 0] din,
	output     [31: 0] dout,

	input              rfs,

	input              cfb_rd,		//the cel engine reads the frame buffer: it takes the word at the end of the phase
	output             rd_wait,		//the console waits for that word
	input              paused,		//the console is stopped (any reason)
	output             row_wait,	//a main-bus read waits for its row (see "Accesses to another row")
	output             mem_wait,	//rd_wait | row_wait, for the console stop (see "Accesses to another row")

	//Block accesses of the VRAM serial port, while the console is stopped (pairs of 16-bit words)
	output reg         v_freeze,
	input              v_frozen,
	input              v_lreq,
	input              v_lwe,
	input      [17: 0] v_laddr,
	input      [ 8: 0] v_lcount,
	input      [31: 0] v_ld,
	output reg         v_lack,
	output             v_lte,
	input              v_rreq,
	input              v_rwe,
	input      [17: 0] v_raddr,
	input      [ 8: 0] v_rcount,
	input      [31: 0] v_rd,
	output reg         v_rack,
	output             v_rte,
	output reg [31: 0] v_q,

	//VRAM rows held only in the row cache of vram.sv (left, right): a main-bus read of such a row waits for its write
	//to the SDRAM, which it asks for (v_lwbq, v_rwbq)
	input      [511:0] v_ldirty,
	input      [511:0] v_rdirty,
	output             v_lwbq,
	output     [ 8: 0] v_lwbq_row,
	output             v_rwbq,
	output     [ 8: 0] v_rwbq_row,
	//main-bus writes into a dirty row go only into the row cache, except into the row vram.sv is writing to the SDRAM
	input              v_lwbp,
	input      [ 8: 0] v_lwbrow,
	input              v_rwbp,
	input      [ 8: 0] v_rwbrow,
	//a main-bus read of a dirty row is taken from the row cache of vram.sv (the row and column, then the word)
	output             v_lcr,
	output     [17: 0] v_lcr_a,
	input              v_lcr_v,
	input      [15: 0] v_lcr_q,
	output             v_rcr,
	output     [17: 0] v_rcr_a,
	input              v_rcr_v,
	input      [15: 0] v_rcr_q

`ifdef DEBUG
	                   ,
	output [1:0] dbg_ctrl_bank,
	output [1:0] dbg_ctrl_cmd,
	output       dbg_ctrl_we,
	output       dbg_ctrl_rfs,

	output       dbg_data_read,
	output       dbg_out_read,
	output       dbg_out_bank,

	output reg [15: 0] dbg_sdram_d,

	output reg [31: 0] dbg_no_refresh,
	output reg [23:0] dbg_numrfs_in_64ms,

	output reg [21:11] dbg_open_lpage,dbg_open_rpage,
	output reg         dbg_cross_page,
	output reg         dbg_hook
`endif
);

	localparam RASCAS_DELAY   = 3'd2; // tRCD=20ns -> 2 cycles
	localparam BURST          = 3'd0; // 0=1, 1=2, 2=4, 3=8, 7=full page
	localparam ACCESS_TYPE    = 1'd0; // 0=sequential, 1=interleaved
	localparam CAS_LATENCY    = 3'd2; // 2/3 allowed
	localparam OP_MODE        = 2'd0; // only 0 (standard operation) allowed
	localparam NO_WRITE_BURST = 1'd1; // 0=write burst enabled, 1=only single access write

	localparam bit [12:0] MODE = {3'b000, NO_WRITE_BURST, OP_MODE, CAS_LATENCY, ACCESS_TYPE, BURST};

	localparam STATE_IDLE  = 3'd0;             // state to check the requests
	localparam STATE_START = STATE_IDLE+1'd1;  // state in which a new command is started
	localparam STATE_CONT  = STATE_START+RASCAS_DELAY;
	localparam STATE_READY = STATE_CONT+CAS_LATENCY+1'd1;
	localparam STATE_LAST  = STATE_READY;      // last state in cycle

	localparam MODE_NORMAL = 2'b00;
	localparam MODE_RESET  = 2'b01;
	localparam MODE_LDM    = 2'b10;
	localparam MODE_PRE    = 2'b11;

	// initialization
	reg [2:0] init_state = '0;
	reg [1:0] mode;
	reg       init_done = 0;
	always @(posedge clk) begin
		reg [4:0] reset = 5'h1f;
		reg init_old = 0;

		if(mode != MODE_NORMAL || init_state != STATE_IDLE || reset) begin
			init_state <= init_state + 1'd1;
			if (init_state == STATE_LAST) init_state <= STATE_IDLE;
		end

		init_old <= init;
		if (init_old & ~init) begin
			reset <= 5'h1f;
			init_done <= 0;
		end
		else if (init_state == STATE_LAST) begin
			if(reset != 0) begin
				reset <= reset - 5'd1;
				if (reset == 15 || reset == 14) begin mode <= MODE_PRE; end
				else if(reset == 4 || reset == 3) begin mode <= MODE_LDM; end
				else                mode <= MODE_RESET;
			end
			else begin
				mode <= MODE_NORMAL;
				init_done <= 1;
			end
		end
	end

	localparam CTRL_IDLE = 2'd0;
	localparam CTRL_RAS = 2'd1;
	localparam CTRL_CAS = 2'd2;
	localparam CTRL_PRE = 2'd3;

	typedef struct packed
	{
		bit [ 1:0] CMD;	//command
		bit [ 1:0] BANK;	//bank
		bit [22:1] ADDR;	//read/write address
		bit [15:0] DATA;	//write data
		bit        RDL;		//read of the left word
		bit        RDR;		//read of the right word
		bit        WE;		//write enable
		bit [ 1:0] BE;		//write byte enable
		bit        RFS;	//refresh
		bit        EP;		//block read: first word of a pair
		bit        ECH;		//block read: right channel
		bit        PALL;	//precharge all banks
		bit        MAIN;	//a column access of the main bus (its row is checked before it goes out)
		bit        CH;		//its channel: 0 left, 1 right
		bit [ 1:0] OBANK;	//the bank and row its channel has open (they do not change in the clock the access is taken)
		bit [22:11] OROW;
	} state_t;
	state_t state[6];
	reg [ 3: 0] st_num;

//	reg rfs_old;
	always @(posedge clk) begin
		reg sync_old;

		sync_old <= sync;
		if (!init_done) begin
			st_num <= 4'd8;
		end else begin
			if (st_num < 4'd8) st_num <= st_num + 4'd1;

			if (sync && !sync_old) begin
				st_num <= 4'd1;
			end

//			rfs_old <= rfs;
		end
	end

	//An auto refresh is due every 7.8 us. The refresh cycles of the console do some of them; the others go in
	//the frames where the main bus is idle, or the console is stopped for them when too many wait.
	reg  [ 9: 0] rfs_div = '0;
	reg  [ 3: 0] rfs_pend = '0;
	wire         rfs_done = state[0].CMD == CTRL_RAS && state[0].RFS && init_done && mode == MODE_NORMAL;
	wire         rfs_force = rfs_pend >= 4'd6;
	always @(posedge clk) begin
		bit tick;

		tick = (rfs_div == 10'd779);
		rfs_div <= tick ? 10'd0 : rfs_div + 10'd1;
		if (tick && !rfs_done && rfs_pend != 4'hF) rfs_pend <= rfs_pend + 4'd1;
		if (!tick && rfs_done && rfs_pend != 4'h0) rfs_pend <= rfs_pend - 4'd1;
	end

	//Rows the main bus keeps open
	reg          lpage_opened = 0,rpage_opened = 0;
	//fix_w8: a write of the console seen while st_num is saturated (8: the console stopped in the middle of a memory cycle and
	//goes on with it) has no slot; it is kept here, one per channel, and goes out in a free clock of that phase, left first
	reg          w8_lp = 0, w8_rp = 0, w8_ls = 0, w8_rs = 0;	//a kept write; the write strobe seen was kept
	reg  [22: 2] w8_la, w8_ra;
	reg  [15: 0] w8_ld, w8_rd;
	reg  [ 1: 0] w8_lbe, w8_rbe, w8_lbank, w8_rbank;
	reg  [ 1: 0] w8_lwr = 0, w8_rwr = 0;	//clocks left before a precharge of the channel after a kept write (tWR)
	reg          w8_lt = 0, w8_rt = 0;	//the write strobe of the channel was taken by its slot (no second copy)
	reg  [ 1: 0] lopen_bank = 2'd0,ropen_bank = 2'd1;
	reg  [22:11] lopen_row = '0,ropen_row = '0;
	wire [ 1: 0] lbank = lvram ? 2'd2 : 2'd0;
	wire [ 1: 0] rbank = rvram ? 2'd3 : 2'd1;

	//Accesses to another row: the main bus opens one row per bank at the start of an access and then only gives
	//columns. The cel engine's two pixels of a cycle can be in two rows of the same half of the VRAM, and a burst
	//can leave its page backwards (mirrored, rotated, stretched cels): such an access is not given to the SDRAM in
	//its slot. A read stops the console at once (row_wait) and is done here, the row opened, before the console
	//goes on; a write is kept (up to X_WQ) and done when the console stops at the end of its memory cycle, as for a
	//block of the serial port (v_freeze).
	//The bus address arrives late in the clock, so the row is not compared in the slot: the access goes into
	//state[0] as before, with the open row of its channel (OBANK, OROW), and the two are compared there, from
	//registers only (x_mis). Such an access does not go out (a NOP instead): a write goes into the queue, a read
	//waits.
	//The kept writes are gathered over several memory cycles: the console is stopped for them when X_WTH wait (X_WQ - X_WTH
	//more can come before it stops at the end of its memory cycle), when the oldest has waited 2.5 us, or with a block or a
	//waiting read (the session does them first). Until then an access of the main bus to a row that has kept writes (the
	//channel's open row, l_qhaz / r_qhaz) does not go out either: a read waits for the session, a write is kept after them,
	//so that the SDRAM sees the writes in their order and a read never gets a word older than a kept write.
	localparam   X_WQ = 8;
	localparam   X_WTH = 4;
	reg          xl_rd = 0,xr_rd = 0;			//a read of the left / right channel waits for its row
	reg  [ 1: 0] xl_rbank,xr_rbank;
	reg  [22: 2] xl_raddr,xr_raddr;
	reg  [ 3: 0] xw_n = '0;						//writes kept
	reg          xw_v [X_WQ];					//still to do (a later full write at the same address cancels it)
	reg          xw_ch [X_WQ];
	reg  [ 1: 0] xw_bank [X_WQ];
	reg  [22: 2] xw_addr [X_WQ];
	reg  [15: 0] xw_data [X_WQ];
	reg  [ 1: 0] xw_be [X_WQ];
	reg  [ 3: 0] l_age = '1,r_age = '1;		//clocks since the row of the channel was opened (tRAS)
	//A read of a VRAM row that is dirty in the row cache of vram.sv (the CPU filled or copied it there) waits too, as a
	//read of another row (x_drd), until vram.sv has written the row to the SDRAM. Whether the channel's open row is
	//dirty is taken a clock after the row opens (lrow_dirty, rrow_dirty), from registers.
	reg          lrow_dirty = 0,rrow_dirty = 0;
	//A main-bus write into a dirty row is not sent to the SDRAM at all: vram.sv keeps the whole row in its cache and takes
	//the write there (the cel engine then draws without changing SDRAM rows); the row goes to the SDRAM in blocks later.
	//Not while vram.sv writes that very row to the SDRAM (its blocks already written would miss the write).
	reg          lrow_wdrop = 0,rrow_wdrop = 0;
	always @(posedge clk) begin
		lrow_dirty <= lpage_opened && lopen_bank == 2'd2 && v_ldirty[lopen_row[19:11]];
		rrow_dirty <= rpage_opened && ropen_bank == 2'd3 && v_rdirty[ropen_row[19:11]];
		lrow_wdrop <= lpage_opened && lopen_bank == 2'd2 && v_ldirty[lopen_row[19:11]] && !(v_lwbp && v_lwbrow == lopen_row[19:11]);
		rrow_wdrop <= rpage_opened && ropen_bank == 2'd3 && v_rdirty[ropen_row[19:11]] && !(v_rwbp && v_rwbrow == ropen_row[19:11]);
	end
	//The channel's open row has kept writes (from registers, a clock after the row opens or a write is kept: the first column
	//access of a row comes later)
	reg          l_qhaz = 0,r_qhaz = 0;
	reg  [ 7: 0] xw_age = '0;					//clocks since the oldest kept write
	always @(posedge clk) begin
		bit lh, rh;
		lh = 0;
		rh = 0;
		for (int i = 0; i < X_WQ; i++) begin
			if (i < xw_n && xw_v[i] && xw_bank[i] == lopen_bank && xw_addr[i][22:11] == lopen_row) lh = 1;
			if (i < xw_n && xw_v[i] && xw_bank[i] == ropen_bank && xw_addr[i][22:11] == ropen_row) rh = 1;
		end
		l_qhaz <= lpage_opened && lh;
		r_qhaz <= rpage_opened && rh;
		xw_age <= xw_n == '0 ? 8'd0 : xw_age != 8'hFF ? xw_age + 8'd1 : xw_age;
	end
	//While the cel engine blends (it reads the frame buffer, cfb_rd, in the last 64 clocks), kept writes are done at the end of
	//each memory cycle: the next frame-buffer read is often in their row and would wait for them anyway
	reg  [ 5: 0] cfb_cnt = '0;
	always @(posedge clk) cfb_cnt <= cfb_rd ? 6'd63 : cfb_cnt != 6'd0 ? cfb_cnt - 6'd1 : 6'd0;
	wire         x_blend = cfb_cnt != 6'd0;
	//The row compare of the access in state[0] (BANK, ADDR row against OBANK, OROW: the open row of its channel when it was
	//taken) is done in pieces when the access is taken, from the same values: three row bits against the open row's in each
	//piece, and the bank, for either channel (lpeq, rpeq, registered every clock as state[0]); in state[0] only the pieces are
	//joined (timing: x_mis drives the command pins, the cache reads of vram.sv and the sessions)
	reg  [ 4: 0] lpeq = '0,rpeq = '0;
	always @(posedge clk) begin
		for (int i = 0; i < 4; i++) begin
			lpeq[i] <= laddr[11 + 3*i +: 3] == lopen_row[11 + 3*i +: 3];
			rpeq[i] <= raddr[11 + 3*i +: 3] == ropen_row[11 + 3*i +: 3];
		end
		lpeq[4] <= lbank == lopen_bank;
		rpeq[4] <= rbank == ropen_bank;
	end
	wire         x_mis = state[0].MAIN && !(state[0].CH ? &rpeq : &lpeq);
	wire         x_drd = state[0].MAIN && !state[0].WE && (state[0].CH ? rrow_dirty : lrow_dirty);
	//(a full write into a row with kept writes goes out at once and cancels those at its address; a read or a byte write waits)
	wire         x_qh = state[0].MAIN && (state[0].CH ? r_qhaz : l_qhaz) && (!state[0].WE || state[0].BE != 2'b11);
	wire         x_hold = x_mis || x_drd || x_qh;		//the access in state[0] does not go out
	wire         x_mis_w = (x_mis || x_qh) && state[0].WE;
	//A read of a dirty row (the open one, x_drd, or another one) is taken from the row cache of vram.sv, which holds the whole
	//row with every write: no session, no write of the row to the SDRAM; the console waits for the word (rd_wait)
	wire         x_cr = state[0].MAIN && !state[0].WE && state[0].BANK[1] &&
	                    (x_mis ? (state[0].CH ? v_rdirty[state[0].ADDR[19:11]] : v_ldirty[state[0].ADDR[19:11]])
	                           : (state[0].CH ? rrow_dirty : lrow_dirty));
	wire         x_mis_r = x_hold && !state[0].WE && !x_cr;
	assign       v_lcr = x_cr && !state[0].CH;
	assign       v_lcr_a = state[0].ADDR[19:2];
	assign       v_rcr = x_cr && state[0].CH;
	assign       v_rcr_a = state[0].ADDR[19:2];
	reg          crw_l = 0,crw_r = 0;			//a read waits for its word from the cache
	always @(posedge clk) begin
		if (v_lcr) crw_l <= 1; else if (v_lcr_v) crw_l <= 0;
		if (v_rcr) crw_r <= 1; else if (v_rcr_v) crw_r <= 0;
	end
	//a write into the open row, dirty: not sent (the cache has it); a write into another row that is dirty: not kept
	wire         x_dw = state[0].MAIN && state[0].WE && !x_mis && !x_qh && (state[0].CH ? rrow_wdrop : lrow_wdrop);
	wire         x_kdrop = state[0].BANK[1] && (state[0].CH ? v_rdirty[state[0].ADDR[19:11]] && !(v_rwbp && v_rwbrow == state[0].ADDR[19:11])
	                                                        : v_ldirty[state[0].ADDR[19:11]] && !(v_lwbp && v_lwbrow == state[0].ADDR[19:11]));
	wire         x_keep = x_mis_w && !x_kdrop;
	//a right read put off by one clock (r_pend) is checked at once from its own copy of the address, so that the
	//console stops as soon as for the other reads
	reg          r_chk = 0;
	reg  [ 1: 0] r_chk_bank;
	reg  [22:11] r_chk_row;
	//(a read of a dirty row waits for the cache instead: r_chk_cr)
	wire         r_chk_cr = r_chk && r_chk_bank == 2'd3 && v_rdirty[r_chk_row[19:11]];
	wire         r_mis_early = r_chk && !r_chk_cr && (r_chk_bank != ropen_bank || r_chk_row != ropen_row || r_qhaz);
	reg          x_rdses = 0;					//a session started for a read: kept writes keep the console stopped
	//a waiting read of a dirty row: vram.sv writes the row to the SDRAM (blocks) when asked, then a session takes the read
	wire         xl_dirty = xl_rbank == 2'd2 && v_ldirty[xl_raddr[19:11]];
	wire         xr_dirty = xr_rbank == 2'd3 && v_rdirty[xr_raddr[19:11]];
	wire         xl_go = xl_rd && !xl_dirty;
	wire         xr_go = xr_rd && !xr_dirty;
	assign       v_lwbq = xl_rd && xl_dirty;
	assign       v_lwbq_row = xl_raddr[19:11];
	assign       v_rwbq = xr_rd && xr_dirty;
	assign       v_rwbq_row = xr_raddr[19:11];

	//A read starts when its request rises or when the phase changes with the request still up. The right word
	//goes one clock after the left one when both start together.
	reg          l_rd_o = 0,r_rd_o = 0,r_pend = 0,sync_o = 0;
	wire         l_rd = lras && lcode == 4'h1;
	wire         r_rd = rras && rcode == 4'h1;
	wire         phase_edge = sync != sync_o;
	wire         l_go = l_rd && (!l_rd_o || phase_edge) && lpage_opened;
	wire         r_new = r_rd && (!r_rd_o || phase_edge) && rpage_opened;
	wire         r_go = r_pend || (r_new && !l_go);
	reg  [ 2: 0] rw_cnt = '0;
	assign rd_wait = rw_cnt != '0 || x_cr || r_chk_cr || crw_l || crw_r;
	always @(posedge clk) begin
		sync_o <= sync;
		l_rd_o <= l_rd;
		r_rd_o <= r_rd;
		r_pend <= r_new && l_go;
		r_chk <= r_new && l_go;
		r_chk_bank <= rbank;
		r_chk_row <= raddr[22:11];
		if (cfb_rd && (l_go || r_new)) rw_cnt <= 3'd4;
		else if (rw_cnt) rw_cnt <= rw_cnt - 3'd1;
	end

	//Block accesses
	localparam E_IDLE   = 5'd0;
	localparam E_FREEZE = 5'd1;
	localparam E_GUARD  = 5'd2;
	localparam E_OPEN   = 5'd3;
	localparam E_ACT    = 5'd4;
	localparam E_RCD    = 5'd5;
	localparam E_XFER   = 5'd6;
	localparam E_POST   = 5'd7;
	localparam E_CLOSE  = 5'd8;
	localparam E_REOPEN = 5'd9;
	localparam E_END2   = 5'd10;
	localparam E_END    = 5'd11;
	localparam E_RPALL  = 5'd12;
	localparam E_RREF   = 5'd13;
	localparam E_RWAIT  = 5'd14;
	localparam E_RACT   = 5'd15;
	localparam E_XNEXT  = 5'd16;	//accesses to another row
	localparam E_XPRE   = 5'd17;
	localparam E_XACT   = 5'd18;
	localparam E_XCAS   = 5'd19;
	localparam E_XDONE  = 5'd20;

	reg  [ 4: 0] e_st = E_IDLE;
	reg          e_active = 0;
	reg          e_refresh = 0;
	reg          e_chan = 0;
	reg          e_last = 0;
	reg  [ 1: 0] e_bank;
	reg  [ 8: 0] e_row;
	reg  [ 8: 0] e_col;
	reg  [ 8: 0] e_cnt;
	reg          e_we;
	reg          e_word;
	reg          e_hit;
	reg          e_restore;
	reg  [ 3: 0] e_wait;
	reg  [ 4: 0] e_ras;
	reg  [ 4: 0] e_hold = '0;
	reg          e_tew_l = 0,e_tew_r = 0;
	reg          e_ract2;
	wire [31: 0] e_wd = e_chan ? v_rd : v_ld;
	reg          x_ch;				//the access to another row being done: channel, write, bank, address, data
	reg          x_we;
	reg  [ 1: 0] x_bank;
	reg  [22: 2] x_addr;
	reg  [15: 0] x_data;
	reg  [ 1: 0] x_be;
	reg          x_cas;
	reg          x_frz = 0;			//the console was stopped for kept writes (v_freeze)
	//(a session started for a read keeps the console stopped to its end: the slots do not run while a session owns
	//the SDRAM, so the console's next access must not come before)
	assign row_wait = xl_rd | xr_rd | x_mis_r | r_mis_early | x_rdses;
	//The same as rd_wait | row_wait, written so that the read held in state[0] stops the console without the dirty-row lookup
	//of its address (x_cr | x_mis_r is x_hold && !WE: x_cr needs x_mis or x_drd): that lookup, a 512-way choice of v_ldirty
	//on state[0].ADDR, was on the path to pause_mem (clk_sys)
	assign mem_wait = rw_cnt != '0 || (x_hold && !state[0].WE) || r_chk_cr || r_mis_early || crw_l || crw_r || xl_rd || xr_rd || x_rdses;

	always @(posedge clk) begin
		bit       main_open;
		bit[22:11] main_row;
		bit       sel_r;

`ifdef DEBUG
		dbg_no_refresh <= dbg_no_refresh + 1'd1;
`endif

		state[0] <= '0;
		//(the address fields count only with a command, CMD, or a main-bus access, MAIN, which set them: by default they take the
		//left channel's access instead of zero, so that a left access needs no late select and no clear of these registers; timing)
		state[0].ADDR  <= {laddr,1'b0};
		state[0].BANK  <= lbank;
		state[0].OBANK <= lopen_bank;
		state[0].OROW  <= lopen_row;
		v_lack <= 0;
		v_rack <= 0;
		e_tew_l <= 0;
		e_tew_r <= 0;
		if (e_hold) e_hold <= e_hold - 5'd1;
		if (l_age != 4'hF) l_age <= l_age + 4'd1;
		if (r_age != 4'hF) r_age <= r_age + 4'd1;
		if (!init_done) begin
			state[0].CMD <= init_state == STATE_START ? CTRL_RAS :
			                init_state == STATE_CONT  ? CTRL_CAS :
								                             CTRL_IDLE;
			state[0].RFS <= 1;
			e_st <= E_IDLE;
			e_active <= 0;
			v_freeze <= 0;
		end else if (e_active) begin
			if (e_ras != '1) e_ras <= e_ras + 5'd1;
			case (e_st)
				E_GUARD: begin
					e_wait <= e_wait - 4'd1;
					if (e_wait == 4'd0) e_st <= e_refresh ? E_RPALL : E_OPEN;
				end

				E_OPEN: begin
					main_open = e_chan ? (rpage_opened && ropen_bank == e_bank) : (lpage_opened && lopen_bank == e_bank);
					main_row = e_chan ? ropen_row : lopen_row;
					e_hit <= 0;
					e_restore <= 0;
					e_wait <= 4'd0;
					e_word <= 0;
					if (main_open && main_row == {3'b000,e_row}) begin
						e_hit <= 1;
						e_st <= E_XFER;
					end else if (main_open) begin
						state[0].CMD <= CTRL_PRE;
						state[0].BANK <= e_bank;
						e_restore <= 1;
						e_wait <= 4'd1;
						e_st <= E_ACT;
					end else begin
						e_st <= E_ACT;
					end
				end

				E_ACT: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else begin
						state[0].CMD <= CTRL_RAS;
						state[0].BANK <= e_bank;
						state[0].ADDR <= {3'b000,e_row,10'b0000000000};
						e_ras <= '0;
						e_st <= E_RCD;
					end
				end

				E_RCD: begin
					e_st <= E_XFER;
				end

				E_XFER: begin
					state[0].CMD <= CTRL_CAS;
					state[0].BANK <= e_bank;
					state[0].ADDR <= {3'b000,e_row,e_col,1'b0};
					if (e_we) begin
						state[0].WE <= 1;
						state[0].BE <= 2'b11;
						state[0].DATA <= !e_word ? e_wd[31:16] : e_wd[15:0];
						if (!e_word) begin
							if (e_chan) e_tew_r <= 1;
							else        e_tew_l <= 1;
						end
					end else begin
						state[0].EP <= !e_word;
						state[0].ECH <= e_chan;
					end
					e_col <= e_col + 9'd1;
					e_word <= ~e_word;
					if (e_word) begin
						e_cnt <= e_cnt - 9'd1;
						if (e_cnt == 9'd1) begin
							e_wait <= e_we ? 4'd2 : 4'd0;
							e_st <= E_POST;
						end
					end
				end

				E_POST: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else if (e_hit) e_st <= E_END;
					else if (e_ras >= 5'd5) e_st <= E_CLOSE;
				end

				E_CLOSE: begin
					state[0].CMD <= CTRL_PRE;
					state[0].BANK <= e_bank;
					e_wait <= 4'd1;
					e_st <= e_restore ? E_REOPEN : E_END2;
				end

				E_REOPEN: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else begin
						state[0].CMD <= CTRL_RAS;
						state[0].BANK <= e_bank;
						state[0].ADDR <= {(e_chan ? ropen_row : lopen_row),10'b0000000000};
						if (e_chan) r_age <= '0; else l_age <= '0;
						e_wait <= 4'd2;
						e_st <= E_END2;
					end
				end

				E_END2: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else e_st <= E_END;
				end

				E_END: begin
					v_freeze <= 0;
					e_active <= 0;
					e_hold <= 5'd16;
					e_st <= E_IDLE;
				end

				E_RPALL: begin
					state[0].CMD <= CTRL_PRE;
					state[0].PALL <= 1;
					e_wait <= 4'd1;
					e_st <= E_RREF;
				end

				E_RREF: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else begin
						state[0].CMD <= CTRL_RAS;
						state[0].RFS <= 1;
						e_wait <= 4'd7;
						e_st <= E_RWAIT;
					end
				end

				E_RWAIT: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else begin
						e_ract2 <= 0;
						e_st <= E_RACT;
					end
				end

				E_RACT: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else if (!e_ract2) begin
						if (lpage_opened) begin
							state[0].CMD <= CTRL_RAS;
							state[0].BANK <= lopen_bank;
							state[0].ADDR <= {lopen_row,10'b0000000000};
							l_age <= '0;
							e_wait <= 4'd1;
						end
						e_ract2 <= 1;
					end else begin
						if (rpage_opened) begin
							state[0].CMD <= CTRL_RAS;
							state[0].BANK <= ropen_bank;
							state[0].ADDR <= {ropen_row,10'b0000000000};
							r_age <= '0;
						end
						e_wait <= 4'd2;
						e_st <= E_END2;
					end
				end

				//Accesses to another row: the kept writes in their order first (they are older than a waiting read,
				//which stopped the console at once, and may be at its address), then the waiting reads
				E_XNEXT: begin
					x_cas <= 0;
					e_wait <= '0;
					if (xw_n != '0) begin
						x_ch <= xw_ch[0]; x_we <= 1; x_bank <= xw_bank[0]; x_addr <= xw_addr[0]; x_data <= xw_data[0]; x_be <= xw_be[0];
						for (int i = 0; i < X_WQ - 1; i++) begin
							xw_v[i] <= xw_v[i + 1];
							xw_ch[i] <= xw_ch[i + 1]; xw_bank[i] <= xw_bank[i + 1]; xw_addr[i] <= xw_addr[i + 1];
							xw_data[i] <= xw_data[i + 1]; xw_be[i] <= xw_be[i + 1];
						end
						xw_n <= xw_n - 4'd1;
						//(a cancelled one is only taken off: the next one in the next clock)
						if (xw_v[0]) e_st <= E_XPRE;
					end else if (xl_go) begin
						x_ch <= 0; x_we <= 0; x_bank <= xl_rbank; x_addr <= xl_raddr;
						e_st <= E_XPRE;
					end else if (xr_go) begin
						x_ch <= 1; x_we <= 0; x_bank <= xr_rbank; x_addr <= xr_raddr;
						e_st <= E_XPRE;
					end else begin
						e_st <= E_XDONE;
					end
				end

				E_XPRE: begin
					//the channel's row: already the access's, or close it (after tRAS) and open the access's
					main_open = x_ch ? rpage_opened : lpage_opened;
					if (main_open && (x_ch ? (ropen_bank == x_bank && ropen_row == x_addr[22:11]) : (lopen_bank == x_bank && lopen_row == x_addr[22:11]))) begin
						e_st <= E_XCAS;
					end else if (main_open) begin
						if ((x_ch ? r_age : l_age) >= 4'd5) begin
							state[0].CMD <= CTRL_PRE;
							state[0].BANK <= x_ch ? ropen_bank : lopen_bank;
							if (x_ch) rpage_opened <= 0; else lpage_opened <= 0;
							e_wait <= 4'd1;
							e_st <= E_XACT;
						end
					end else begin
						e_st <= E_XACT;
					end
				end

				E_XACT: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else begin
						state[0].CMD <= CTRL_RAS;
						state[0].BANK <= x_bank;
						state[0].ADDR <= {x_addr,1'b0};
						if (x_ch) begin rpage_opened <= 1; ropen_bank <= x_bank; ropen_row <= x_addr[22:11]; r_age <= '0; end
						else      begin lpage_opened <= 1; lopen_bank <= x_bank; lopen_row <= x_addr[22:11]; l_age <= '0; end
						e_wait <= 4'd1;
						e_st <= E_XCAS;
					end
				end

				E_XCAS: begin
					if (e_wait) e_wait <= e_wait - 4'd1;
					else if (!x_cas) begin
						state[0].CMD <= CTRL_CAS;
						state[0].BANK <= x_bank;
						state[0].ADDR <= {x_addr,1'b0};
						if (x_we) begin
							state[0].WE <= 1;
							state[0].BE <= x_be;
							state[0].DATA <= x_data;
							e_wait <= 4'd2;	//tWR
						end else begin
							if (x_ch) state[0].RDR <= 1; else state[0].RDL <= 1;
							e_wait <= 4'd5;	//until the word is in dout
						end
						x_cas <= 1;
					end else begin
						if (!x_we) begin if (x_ch) xr_rd <= 0; else xl_rd <= 0; end
						e_st <= E_XNEXT;
					end
				end

				E_XDONE: begin
					if (x_frz) v_freeze <= 0;
					x_frz <= 0;
					x_rdses <= 0;
					e_active <= 0;
					e_st <= E_IDLE;
				end

				default:;
			endcase
		end else begin
			if (e_st == E_IDLE) begin
				if (xl_go || xr_go || x_mis_r) begin
					//a read waits for its row: the console stops at once (row_wait); a read of a dirty row waits for
					//the write of the row (the blocks below) first
					if (paused && st_num == 4'd8 && (xl_go || xr_go)) begin
						e_active <= 1;
						x_rdses <= 1;
						e_st <= E_XNEXT;
					end
				end else if (xw_n >= X_WTH || (x_keep && xw_n == X_WTH - 1) || xw_age == 8'hFF || (x_blend && (xw_n != '0 || x_keep))) begin
					//kept writes: the console stops at the end of its memory cycle
					v_freeze <= 1;
					x_frz <= 1;
					e_st <= E_FREEZE;
				end else if (!e_hold && (v_lreq || v_rreq || rfs_force)) begin
					v_freeze <= 1;
					e_st <= E_FREEZE;
				end
			end else if (e_st == E_FREEZE) begin
				if (v_frozen && st_num == 4'd8 && (x_frz || xl_go || xr_go || xw_n != '0 || x_hold)) begin
					x_frz <= 1;
					e_active <= 1;
					e_st <= E_XNEXT;
				end else if (v_frozen && st_num == 4'd8) begin
					//(the guard leaves vram.sv the time to read the first chunk of a dirty row from its cache once the
					//console's last write is in)
					e_active <= 1;
					e_refresh <= rfs_force;
					e_st <= E_GUARD;
					e_wait <= 4'd6;
					if (!rfs_force) begin
						sel_r = v_rreq && (!v_lreq || !e_last);
						e_last <= sel_r;
						e_chan <= sel_r;
						e_bank <= sel_r ? 2'd3 : 2'd2;
						e_row <= sel_r ? v_raddr[17:9] : v_laddr[17:9];
						e_col <= sel_r ? v_raddr[8:0] : v_laddr[8:0];
						e_cnt <= sel_r ? v_rcount : v_lcount;
						e_we <= sel_r ? v_rwe : v_lwe;
						v_rack <= sel_r;
						v_lack <= !sel_r;
					end
				end
			end

			//The write data and byte mask of the slot's channel (left in the even slots, right in the odd ones) are
			//taken in every slot: they only count for a write (WE, CMD), and so the bus control signals, which arrive
			//late in the clock, do not select them
			state[0].DATA <= st_num[0] ? din[15:0] : din[31:16];
			state[0].BE   <= st_num[0] ? rwe : lwe;

			if (lras && lcode == 4'h8 && st_num[2:0] == 3'd5) begin
				state[0].CMD  <= CTRL_RAS;
				state[0].ADDR <= {laddr,1'b0};
				state[0].BANK <= lbank;
				lpage_opened <= 1;
				lopen_bank <= lbank;
				lopen_row <= laddr[22:11];
				l_age <= '0;
`ifdef DEBUG
				dbg_cross_page <= 0;
				dbg_open_lpage <= laddr[21:11];
`endif
			end
			if (rras && rcode == 4'h8 && st_num[2:0] == 3'd6) begin
				state[0].CMD  <= CTRL_RAS;
				state[0].ADDR <= {raddr,1'b0};
				state[0].BANK <= rbank;
				rpage_opened <= 1;
				ropen_bank <= rbank;
				ropen_row <= raddr[22:11];
				r_age <= '0;
`ifdef DEBUG
				dbg_cross_page <= 0;
				dbg_open_rpage <= raddr[21:11];
`endif
			end

			if (l_go) begin
				state[0].CMD  <= CTRL_CAS;
				state[0].ADDR <= {laddr,1'b0};
				state[0].RDL  <= 1;
				state[0].BANK <= lbank;
				state[0].MAIN <= 1;
				state[0].CH   <= 0;
				state[0].OBANK <= lopen_bank;
				state[0].OROW <= lopen_row;
`ifdef DEBUG
				dbg_cross_page <= (dbg_open_lpage != laddr[21:11]);
`endif
			end
			if (r_go) begin
				state[0].CMD  <= CTRL_CAS;
				state[0].ADDR <= {raddr,1'b0};
				state[0].RDR  <= 1;
				state[0].BANK <= rbank;
				state[0].MAIN <= 1;
				state[0].CH   <= 1;
				state[0].OBANK <= ropen_bank;
				state[0].OROW <= ropen_row;
`ifdef DEBUG
				dbg_cross_page <= (dbg_open_rpage != raddr[21:11]);
`endif
			end

			if (lras && lcode == 4'h2 && (st_num[2:0] == 3'd2 || st_num[2:0] == 3'd6) && lpage_opened) begin
				w8_lt <= 1;	//fix_w8: taken by its slot
				state[0].CMD  <= CTRL_CAS;
				state[0].ADDR <= {laddr,1'b0};
				state[0].WE   <= 1;
				state[0].BANK <= lbank;
				state[0].MAIN <= 1;
				state[0].CH   <= 0;
				state[0].OBANK <= lopen_bank;
				state[0].OROW <= lopen_row;
`ifdef DEBUG
				dbg_cross_page <= (dbg_open_lpage != laddr[21:11]);
				if (dbg_open_lpage == (22'h0B9A6C>>11) && laddr[10:2] == ((22'h0B9A6C&22'h0007FF)>>2)) begin
					dbg_hook <= (din[31:16] == 16'h00FF);
				end
`endif
			end
			if (rras && rcode == 4'h2 && (st_num[2:0] == 3'd3 || st_num[2:0] == 3'd7) && rpage_opened) begin
				w8_rt <= 1;	//fix_w8: taken by its slot
				state[0].CMD  <= CTRL_CAS;
				state[0].ADDR <= {raddr,1'b0};
				state[0].WE   <= 1;
				state[0].BANK <= rbank;
				state[0].MAIN <= 1;
				state[0].CH   <= 1;
				state[0].OBANK <= ropen_bank;
				state[0].OROW <= ropen_row;
`ifdef DEBUG
				dbg_cross_page <= (dbg_open_rpage != raddr[21:11]);
`endif
			end

			//fix_w8: a write seen while st_num is saturated is kept, then sent in a clock of that phase without another command
			if (w8_lwr != 2'd0) w8_lwr <= w8_lwr - 2'd1;
			if (w8_rwr != 2'd0) w8_rwr <= w8_rwr - 2'd1;
			if (st_num == 4'd8 && lras && lcode == 4'h2 && |lwe && lpage_opened && !w8_ls && !w8_lp && !w8_lt) begin
				w8_lp <= 1; w8_la <= laddr; w8_ld <= din[31:16]; w8_lbe <= lwe; w8_lbank <= lbank; w8_ls <= 1;
			end else if (!(lras && lcode == 4'h2 && |lwe)) begin w8_ls <= 0; w8_lt <= 0; end
			if (st_num == 4'd8 && rras && rcode == 4'h2 && |rwe && rpage_opened && !w8_rs && !w8_rp && !w8_rt) begin
				w8_rp <= 1; w8_ra <= raddr; w8_rd <= din[15:0]; w8_rbe <= rwe; w8_rbank <= rbank; w8_rs <= 1;
			end else if (!(rras && rcode == 4'h2 && |rwe)) begin w8_rs <= 0; w8_rt <= 0; end
			//(data and byte mask of a kept write by registered signals only, as for the slots: they count only with a write)
			if (st_num == 4'd8 && w8_lp) begin state[0].DATA <= w8_ld; state[0].BE <= w8_lbe; end
			else if (st_num == 4'd8 && w8_rp) begin state[0].DATA <= w8_rd; state[0].BE <= w8_rbe; end
			if (st_num != 4'd8) begin
				w8_lp <= 0;
				w8_rp <= 0;
			//(not 2 or 3 clocks after a read: its data is then on DQ)
			end else if ((w8_lp || w8_rp) && !l_go && !r_go && !(!lras && lpage_opened) &&
			             !(state[2].RDL || state[2].RDR || state[3].RDL || state[3].RDR)) begin
				state[0].CMD  <= CTRL_CAS;
				state[0].WE   <= 1;
				state[0].MAIN <= 1;
				if (w8_lp) begin
					state[0].ADDR  <= {w8_la,1'b0};
					state[0].BANK  <= w8_lbank;
					state[0].CH    <= 0;
					state[0].OBANK <= lopen_bank;
					state[0].OROW  <= lopen_row;
					w8_lp <= 0;
					w8_lwr <= 2'd3;
				end else begin
					state[0].ADDR  <= {w8_ra,1'b0};
					state[0].BANK  <= w8_rbank;
					state[0].CH    <= 1;
					state[0].OBANK <= ropen_bank;
					state[0].OROW  <= ropen_row;
					w8_rp <= 0;
					w8_rwr <= 2'd3;
				end
			end

			//the access in state[0] is of another row (x_mis, it does not go out): a write is kept for the end of the
			//memory cycle, a read waits for its row
			//a full write of the main bus (sent now, taken by the cache, or kept) cancels the older kept writes at its address,
			//which would otherwise land after it
			if (state[0].MAIN && state[0].WE && state[0].BE == 2'b11) begin
				for (int i = 0; i < X_WQ; i++)
					if (i < xw_n && xw_bank[i] == state[0].BANK && xw_addr[i] == state[0].ADDR[22:2]) xw_v[i] <= 0;
			end
			//the entry after the kept writes takes every main write (x_keep implies state[0].MAIN && state[0].WE): it is not
			//part of the queue until xw_n counts it, and every reader stops at xw_n, so only xw_v and xw_n wait for x_keep,
			//which comes late (dirty-row lookup); same queue contents as writing the entry on x_keep alone
			if (state[0].MAIN && state[0].WE && xw_n < X_WQ) begin
				xw_ch[xw_n] <= state[0].CH;
				xw_bank[xw_n] <= state[0].BANK;
				xw_addr[xw_n] <= state[0].ADDR[22:2];
				xw_data[xw_n] <= state[0].DATA;
				xw_be[xw_n] <= state[0].BE;
			end
			if (x_keep && xw_n < X_WQ) begin
				xw_v[xw_n] <= 1;
				xw_n <= xw_n + 4'd1;
			end
			if (x_mis_r && !state[0].CH && !xl_rd) begin
				xl_rd <= 1;
				xl_rbank <= state[0].BANK;
				xl_raddr <= state[0].ADDR[22:2];
			end
			if (x_mis_r && state[0].CH && !xr_rd) begin
				xr_rd <= 1;
				xr_rbank <= state[0].BANK;
				xr_raddr <= state[0].ADDR[22:2];
			end

			//(not while a row is open: a session for kept writes can leave one open just before the console's refresh cycle,
			//whose RAS keeps the slot precharges away; the refresh is then owed, rfs_pend)
			if (lras && (lcode == 4'h4 || lcode == 4'hC) && st_num[2:0] == 3'd4 && !lpage_opened && !rpage_opened) begin
				state[0].CMD  <= CTRL_RAS;
				state[0].BANK <= 2'd0;
				state[0].RFS  <= 1;
			end
			else if (!lras && !rras && !lpage_opened && !rpage_opened && lcode == 4'h0 && rcode == 4'h0 && rfs_pend != 4'h0 && st_num[2:0] == 3'd4) begin
				state[0].CMD  <= CTRL_RAS;
				state[0].BANK <= 2'd0;
				state[0].RFS  <= 1;
			end

			//(not while a session runs: the row a session opens for an access to another row stays open for its CAS)
			if (!lras && lpage_opened && st_num[2:0] == 3'd0 && !e_active && w8_lwr == 2'd0) begin
				state[0].CMD  <= CTRL_PRE;
				state[0].BANK <= lopen_bank;
				lpage_opened <= 0;
			end
			if (!rras && rpage_opened && st_num[2:0] == 3'd1 && !e_active && w8_rwr == 2'd0) begin
				state[0].CMD  <= CTRL_PRE;
				state[0].BANK <= ropen_bank;
				rpage_opened <= 0;
			end
		end
	end
	always @(posedge clk) begin
		state[1] <= state[0];
		if (x_hold) begin
			//not sent: no word to take
			state[1].RDL <= 0;
			state[1].RDR <= 0;
		end
		state[2] <= state[1];
		state[3] <= state[2];
		state[4] <= state[3];
		state[5] <= state[4];
	end

	//An access to another row (x_mis) goes out as a NOP: only the command pins and the drive of the data pins look at
	//x_mis. The address and bank pins take the access as before (a NOP does not look at them, and the mask of a read
	//is given by the read command itself), so that the row compare only drives a few pins.
	wire [ 1:0] ctrl_cmd   = (x_hold || x_dw) ? CTRL_IDLE : state[0].CMD;
	wire [ 1:0] ctrl_cmdp  = state[0].CMD;
	wire [22:1] ctrl_addr  = state[0].ADDR;
	wire [15:0] ctrl_data  = state[0].DATA;
//	wire        ctrl_rd    = state[0].RD;
	wire        ctrl_we    = state[0].WE;
	wire [ 1:0] ctrl_be    = state[0].BE;
	wire [ 1:0] ctrl_bank  = state[0].BANK;
	wire        ctrl_rfs   = state[0].RFS;
	wire        ctrl_pall  = state[0].PALL;

//	wire       data_read0 = state[3].RD;
//	wire       data_read1  = state[4].RD;
//	wire       out_read0  = state[4].RD;
//	wire       out_bank  = state[5].BANK[0];

	reg [31:0] rbuf;
	reg        v_rdte_l = 0,v_rdte_r = 0;
	always @(posedge clk) begin
		rbuf <= {rbuf[15:0],SDRAM_DQ};
		//The two words stay until the next read of their own channel: the console may be stopped before it takes them
		if (state[4].RDL) dout[31:16] <= rbuf[15:0];
		if (state[4].RDR) dout[15: 0] <= rbuf[15:0];
		//a read of a dirty row: the word from the row cache
		if (v_lcr_v) dout[31:16] <= v_lcr_q;
		if (v_rcr_v) dout[15: 0] <= v_rcr_q;

		v_rdte_l <= 0;
		v_rdte_r <= 0;
		if (state[5].EP) begin
			v_q <= rbuf;
			if (state[5].ECH) v_rdte_r <= 1;
			else              v_rdte_l <= 1;
		end
	end
	assign v_lte = v_rdte_l | e_tew_l;
	assign v_rte = v_rdte_r | e_tew_r;


	localparam CMD_NOP             = 3'b111;
	localparam CMD_ACTIVE          = 3'b011;
	localparam CMD_READ            = 3'b101;
	localparam CMD_WRITE           = 3'b100;
	localparam CMD_BURST_TERMINATE = 3'b110;
	localparam CMD_PRECHARGE       = 3'b010;
	localparam CMD_AUTO_REFRESH    = 3'b001;
	localparam CMD_LOAD_MODE       = 3'b000;

	// SDRAM state machines
	wire [22:1] a = ctrl_addr;
	wire [15:0] d = ctrl_data;
	wire  [1:0] dqm = ~ctrl_be;
	always @(posedge clk) begin
		if (ctrl_cmdp == CTRL_RAS || ctrl_cmdp == CTRL_CAS || ctrl_cmdp == CTRL_PRE) SDRAM_BA <= (mode == MODE_NORMAL) ? ctrl_bank : 2'b00;

		casex({init_done,ctrl_rfs,ctrl_we,mode,ctrl_cmd})
			{3'bX0X, MODE_NORMAL, CTRL_RAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_ACTIVE,1'b0};
			{3'bX1X, MODE_NORMAL, CTRL_RAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_AUTO_REFRESH,1'b0};
			{3'b101, MODE_NORMAL, CTRL_CAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_WRITE,1'b0};
			{3'b100, MODE_NORMAL, CTRL_CAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_READ,1'b0};
			{3'b10X, MODE_NORMAL, CTRL_PRE}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_PRECHARGE,1'b0};

			// init
			{3'bXXX,    MODE_LDM, CTRL_RAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_LOAD_MODE, 1'b0};
			{3'bXXX,    MODE_PRE, CTRL_RAS}: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_PRECHARGE, 1'b0};

										   default: {SDRAM_nRAS, SDRAM_nCAS, SDRAM_nWE, SDRAM_nCS} <= {CMD_NOP,1'b1};
		endcase

		SDRAM_DQ <= 'Z;
		casex({init_done,ctrl_rfs,ctrl_we,mode,ctrl_cmd})
			{3'b101, MODE_NORMAL, CTRL_CAS}: begin
				SDRAM_DQ <= d;
`ifdef DEBUG
				dbg_sdram_d <= d;
`endif
			end
										   default: ;
		endcase

		if (mode == MODE_NORMAL) begin
			casex ({ctrl_we,ctrl_cmdp})
				{1'bX,CTRL_RAS}: SDRAM_A <= {1'b0,a[22:11]};
				{1'b0,CTRL_CAS}: SDRAM_A <= {2'b00,1'b0,a[10:1]};
				{1'b1,CTRL_CAS}: SDRAM_A <= {dqm  ,1'b0,a[10:1]};
				{1'bX,CTRL_PRE}: SDRAM_A <= {2'b00,ctrl_pall,10'b0000000000};
			endcase;
		end
		else if (mode == MODE_LDM && ctrl_cmdp == CTRL_RAS) SDRAM_A <= MODE;
		else if (mode == MODE_PRE && ctrl_cmdp == CTRL_RAS) SDRAM_A <= 13'b0010000000000;
		else SDRAM_A <= '0;
	end

	assign SDRAM_CKE = 1;
	assign {SDRAM_DQMH,SDRAM_DQML} = SDRAM_A[12:11];


`ifdef DEBUG
	always @(posedge clk) begin
		reg [31: 0] div_cnt;
		reg [23: 0] num_cnt;

		if (ctrl_rfs) num_cnt <= num_cnt + 32'd1;

		div_cnt <= div_cnt + 1;
		if (div_cnt == 6400000) begin
			div_cnt <= '0;
			dbg_numrfs_in_64ms <= num_cnt;
			num_cnt <= '0;
		end
	end
	assign dbg_ctrl_bank = ctrl_bank;
	assign dbg_ctrl_cmd = ctrl_cmd;
	assign dbg_ctrl_we = ctrl_we;
	assign dbg_ctrl_rfs = ctrl_rfs;
	assign dbg_data_read = data_read0;
	assign dbg_out_read = out_read0;
	assign dbg_out_bank = out_bank;
`endif

	altddio_out
	#(
		.extend_oe_disable("OFF"),
		.intended_device_family("Cyclone V"),
		.invert_output("OFF"),
		.lpm_hint("UNUSED"),
		.lpm_type("altddio_out"),
		.oe_reg("UNREGISTERED"),
		.power_up_high("OFF"),
		.width(1)
	)
	sdramclk_ddr
	(
		.datain_h(1'b0),
		.datain_l(1'b1),
		.outclock(clk),
		.dataout(SDRAM_CLK),
		.aclr(1'b0),
		.aset(1'b0),
		.oe(1'b1),
		.outclocken(1'b1),
		.sclr(1'b0),
		.sset(1'b0)
	);

endmodule
