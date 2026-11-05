//============================================================================
//  Memory pause meter (debug option).
//
//  The console is paused while it waits for the DDR3 (the part of the VRAM that
//  does not fit in the FPGA memory, the BIOS ROM, the NVRAM): the CPU and MADAM
//  lose that time while the video keeps running, which slows games down. This
//  measures, for each frame, the share of the time spent paused, in total and
//  by cause, and draws one bar per measure over the top-left of the picture:
//    1. red     all pauses
//    2. yellow  VRAM in DDR3: random access not in the cache (ddr_busy)
//    3. cyan    VRAM page transfer (SPORT) still running (VRAM_LRDY/RRDY low)
//    4. green   BIOS ROM or NVRAM access in DDR3 (ddr_io_busy)
//  A bar is 256 pixels long for 100 %, with grey marks every 25 %; a white
//  mark holds the highest value of the last 64 frames.
//============================================================================
module P3DO_PAUSE_METER
(
	input             CLK,         // clk_sys
	input             PAUSE,       // the console is paused
	input             DDR_BUSY,    // causes, counted while PAUSE is set
	input             VRAM_WAIT,
	input             IO_BUSY,
	input             ENABLE,      // OSD option (CLK domain)

	input             VCLK,        // video clock
	input             CE_PIX,
	input             HBL_N,
	input             VBL_N,
	input      [23:0] RGB_IN,
	output     [23:0] RGB_OUT      // RGB_IN unchanged when the option is off
);

	//------------------------------------------------------------------------
	// Measure (CLK domain)
	//------------------------------------------------------------------------
	reg  [ 2:0] vbl_sync;
	always @(posedge CLK) vbl_sync <= {vbl_sync[1:0], VBL_N};
	wire        frame_end = vbl_sync[2] & ~vbl_sync[1];	// start of the vertical blank

	reg  [23:0] frame_cnt;
	reg  [23:0] cnt[4];
	reg  [23:0] snap_frame;
	reg  [23:0] snap[4];
	reg  [ 7:0] bar[4], peak[4];
	reg  [ 5:0] peak_age;
	reg         shadow_tgl;

	// Division of each count by the frame length: bar = 256 * count / frame (0..255, 100 % = 255)
	reg  [ 2:0] div_idx = 3'd5;	// 0..3 bar being computed, 4 peaks, 5 idle
	reg  [ 3:0] div_bit;
	reg  [23:0] div_rem;	// stays below the frame length
	reg  [ 7:0] div_q;
	wire [24:0] div_rem2 = {div_rem, 1'b0};
	wire [24:0] div_sub = div_rem2 - {1'b0, snap_frame};

	always @(posedge CLK) begin
		if (frame_end) begin
			snap_frame <= frame_cnt;
			snap[0] <= cnt[0];
			snap[1] <= cnt[1];
			snap[2] <= cnt[2];
			snap[3] <= cnt[3];
			frame_cnt <= 24'd1;
			cnt[0] <= {23'd0, PAUSE};
			cnt[1] <= {23'd0, PAUSE & DDR_BUSY};
			cnt[2] <= {23'd0, PAUSE & VRAM_WAIT};
			cnt[3] <= {23'd0, PAUSE & IO_BUSY};
			div_idx <= 3'd0;
			div_bit <= 4'd0;
		end
		else begin
			if (~&frame_cnt) frame_cnt <= frame_cnt + 24'd1;
			if (PAUSE && ~&cnt[0]) cnt[0] <= cnt[0] + 24'd1;
			if (PAUSE && DDR_BUSY && ~&cnt[1]) cnt[1] <= cnt[1] + 24'd1;
			if (PAUSE && VRAM_WAIT && ~&cnt[2]) cnt[2] <= cnt[2] + 24'd1;
			if (PAUSE && IO_BUSY && ~&cnt[3]) cnt[3] <= cnt[3] + 24'd1;

			if (!div_idx[2]) begin
				if (div_bit == 4'd0) begin
					div_rem <= snap[div_idx[1:0]];
					div_q <= '0;
					div_bit <= 4'd1;
				end
				else if (div_bit <= 4'd8) begin
					// one quotient bit per clock: r = 2r, q = 2q + (r >= frame)
					if (!div_sub[24]) begin
						div_rem <= div_sub[23:0];
						div_q <= {div_q[6:0], 1'b1};
					end
					else begin
						div_rem <= div_rem2[23:0];
						div_q <= {div_q[6:0], 1'b0};
					end
					div_bit <= div_bit + 4'd1;
				end
				else begin
					// count == frame gives 256 * frame / frame: shown as 255
					bar[div_idx[1:0]] <= (snap[div_idx[1:0]] >= snap_frame && snap_frame != 0) ? 8'd255 : div_q;
					div_bit <= 4'd0;
					div_idx <= div_idx + 3'd1;
				end
			end
		end

		// Peaks, refreshed once all bars of the frame are computed
		if (div_idx == 3'd4) begin
			div_idx <= 3'd5;
			peak_age <= peak_age + 6'd1;
			for (int i = 0; i < 4; i++) begin
				if (bar[i] > peak[i] || peak_age == 6'd63) peak[i] <= bar[i];
			end
			shadow_tgl <= ~shadow_tgl;
		end
	end

	//------------------------------------------------------------------------
	// Draw (VCLK domain)
	//------------------------------------------------------------------------
	reg  [ 2:0] tgl_sync, en_sync;
	reg  [ 7:0] vbar[4], vpeak[4];
	always @(posedge VCLK) begin
		tgl_sync <= {tgl_sync[1:0], shadow_tgl};
		en_sync <= {en_sync[1:0], ENABLE};
		if (tgl_sync[2] != tgl_sync[1]) begin	// the bars are stable for a whole frame after the toggle
			for (int i = 0; i < 4; i++) begin
				vbar[i] <= bar[i];
				vpeak[i] <= peak[i];
			end
		end
	end

	// Bars of 6 lines every 8 lines from line Y0, 256 pixels from pixel X0, on a dark box
	localparam [9:0] X0 = 10'd16;
	localparam [9:0] Y0 = 10'd24;

	// Position of the pixel on RGB_IN: x counts the pixels of the active line, y the active lines
	reg  [ 9:0] x;
	reg  [ 9:0] y;
	reg         hbl_old;
	always @(posedge VCLK) begin
		if (CE_PIX) begin
			hbl_old <= HBL_N;
			if (!HBL_N) x <= '0;
			else if (~&x) x <= x + 10'd1;
			if (!VBL_N) y <= '0;
			else if (hbl_old && !HBL_N && ~&y) y <= y + 10'd1;	// end of an active line
		end
	end

	wire [ 7:0] bx = x[7:0] - X0[7:0];	// position in the bar (only used inside it)
	wire [ 4:0] by = y[4:0] - Y0[4:0];
	wire [ 1:0] row = by[4:3];
	wire        in_box = x + 10'd1 >= X0 && x <= X0 + 10'd256 && y + 10'd1 >= Y0 && y < Y0 + 10'd31;
	wire        in_bar = x >= X0 && x < X0 + 10'd256 && y >= Y0 && y < Y0 + 10'd32 && by[2:0] < 3'd6;

	function automatic [23:0] Color(input [1:0] i);
		case (i)
			2'd0: Color = 24'hFF4040;
			2'd1: Color = 24'hFFD000;
			2'd2: Color = 24'h00D0FF;
			2'd3: Color = 24'h40FF40;
		endcase
	endfunction

	reg  [23:0] ovl;
	always_comb begin
		ovl = 24'h202020;
		if (in_bar) begin
			if (bx[7:0] == vpeak[row] && vpeak[row] != 8'd0) ovl = 24'hFFFFFF;
			else if (bx[7:0] < vbar[row]) ovl = Color(row);
			else if (bx[5:0] == 6'd63) ovl = 24'h707070;
		end
	end
	assign RGB_OUT = en_sync[2] && in_box ? ovl : RGB_IN;

endmodule
