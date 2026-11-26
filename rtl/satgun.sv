//============================================================================
//  Sega Saturn light gun (Virtua Gun / Stunner) on the SNAC port (Saturn adapter), read as the light gun of the 3DO.
//  2026-11-24 (dots/satgun_patch.py).
//
//  The gun does not send a place: its sensor pulls TH low while it sees the light of the CRT, and the console notes
//  where its beam is at that moment (the Saturn latches its counters when TH falls). Pins as the MiSTer Saturn core and
//  its SNAC adapter (Saturn.sv: {TH, TR, TL, R, L, D, U} = USER_IN[4], [6], [2], [3], [5], [0], [1]; hps2pad.sv, the
//  emulated gun: {!SENSOR, !START, !TRIGGER, 4'b1100}): 4 TH light (low: seen), 6 TR Start (low: pressed), 2 TL trigger
//  (low: pressed); 0 D and 1 U low, 3 R and 5 L high: the gun's id. All pins are inputs (USER_OUT high).
//
//  For each field: the first place seen (where TH is first low, taken back by DLY dots: the time the picture takes from
//  the core's crosshair stage to the CRT plus the time the gun takes to answer), and the number of lines on which TH was
//  low, handed to clk_sys DLY + 4 dots after the start of the vertical blank. These are the results the core's picture
//  sensor (P3DO_GUN_VIDEO) gives for a gun aimed at the same place, in the same units (dot of the active line 0-639,
//  active line), so the 3DO gun (P3DO_LIGHTGUN, SENSE) turns them into its counter the same way. A TH that never goes
//  high in a field (no gun, another adapter) gives no place.
//============================================================================
module P3DO_SATGUN
(
	input             CLK,          // clk_sys
	input             ENABLE,
	input      [ 6:0] USER_IN,
	input      [10:0] DLY,          // signed: dots from a place of the picture (at the crosshair stage) to TH low here

	output reg        LIT = 1'b0,   // the gun saw light in the last field
	output reg [ 9:0] LIT_X = '0,   //   the first place: dot, line
	output reg [ 7:0] LIT_Y = '0,
	output reg [ 4:0] LIT_N = '0,   //   lines on which it saw light
	output reg        TRIGGER = 1'b0,
	output reg        START = 1'b0,
	output reg        PRESENT = 1'b0, // the id of the gun is on the pins

	input             VCLK,         // video clock
	input             CE_PIX,
	input             HBL_N,
	input             VBL_N
);

	//------------------------------------------------------------------ buttons (clk_sys)
	reg  [ 6:0] u1 = '1, u2 = '1;
	always @(posedge CLK) begin
		u1 <= USER_IN;
		u2 <= u1;
		PRESENT <= ENABLE && !u2[0] && !u2[1] && u2[3] && u2[5];
		TRIGGER <= PRESENT && !u2[2];
		START <= PRESENT && !u2[6];
	end

	//------------------------------------------------------------------ sensor (video clock)
	reg  [ 2:0] th_s = '1, en_s = '0;
	always @(posedge VCLK) begin
		th_s <= {th_s[1:0], USER_IN[4]};
		en_s <= {en_s[1:0], ENABLE};
	end
	wire        th_low = en_s[2] && !th_s[2];

	// the setting, copied in each vertical blank (it changes only from the menu)
	reg  signed [10:0] dly = '0;
	wire [ 8:0] ho_len = dly[10] ? 9'd4 : {1'b0, dly[7:0]} + 9'd4;

	reg         hbl_o = 1'b0, vbl_o = 1'b0;
	reg  [10:0] sx = '0;            // dots since the start of the active line, through the blank that follows
	reg  [ 8:0] sy = '1;            // that line; all ones before the first active line of the field
	reg         armed = 1'b0;       // TH was high in this field
	reg         f = 1'b0, l = 1'b0; // first place found, light on the current line
	reg  [ 9:0] fx = '0;
	reg  [ 7:0] fy = '0;
	reg  [ 4:0] n = '0;
	reg  [ 8:0] ho = '0;            // dots left to the hand-over after the start of the vertical blank (0: none)
	reg         r_f = 1'b0, r_tgl = 1'b0;
	reg  [ 9:0] r_x = '0;
	reg  [ 7:0] r_y = '0;
	reg  [ 4:0] r_n = '0;

	wire        a_start = HBL_N && !hbl_o && VBL_N;	// first dot of an active line
	wire [10:0] cx = a_start ? 11'd0 : sx;
	wire [ 8:0] cy = a_start ? sy + 9'd1 : sy;
	wire signed [11:0] px = $signed({1'b0, cx}) - $signed({dly[10], dly});	// the place seen
	wire        on_line = !(&cy) && px >= 12'sd0;
	wire        seen = th_low && on_line;
	wire [ 9:0] px_c = px > 12'sd639 ? 10'd639 : px[9:0];

	always @(posedge VCLK) begin
		if (CE_PIX) begin
			hbl_o <= HBL_N;
			vbl_o <= VBL_N;
			sx <= ~&cx ? cx + 11'd1 : cx;
			if (!th_low) armed <= 1'b1;
			if (a_start) begin
				sy <= cy;
				if (l && ~&n) n <= n + 5'd1;	// the line before had light
				l <= 1'b0;
			end
			if (seen) begin
				l <= 1'b1;
				if (armed && !f) begin	// the first place of the field
					f <= 1'b1;
					fx <= px_c;
					fy <= cy[7:0];
				end
			end
			if (vbl_o && !VBL_N) ho <= ho_len;	// start of the vertical blank
			else if (ho != 9'd0) begin
				ho <= ho - 9'd1;
				if (ho == 9'd1) begin	// the light of the last line is in: results of the field
					r_f <= f;
					r_x <= fx;
					r_y <= fy;
					r_n <= (l || seen) && ~&n ? n + 5'd1 : n;
					r_tgl <= ~r_tgl;
					f <= 1'b0;
					l <= 1'b0;
					n <= 5'd0;
					armed <= !th_low;
					sy <= '1;
				end
			end
			else if (!VBL_N && !vbl_o) begin	// vertical blank, after the hand-over
				sy <= '1;
				dly <= $signed(DLY);
			end
		end
	end

	// results to clk_sys: stable for a whole field after the toggle
	reg  [ 2:0] r_sync = '0;
	always @(posedge CLK) begin
		r_sync <= {r_sync[1:0], r_tgl};
		if (r_sync[2] != r_sync[1]) begin
			LIT <= r_f && ENABLE;
			LIT_X <= r_x;
			LIT_Y <= r_y;
			LIT_N <= r_n;
		end
		if (!ENABLE) LIT <= 1'b0;
	end

endmodule
