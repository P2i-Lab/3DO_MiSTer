//============================================================================
//  Screen information overlay (debug option "Screens/cel/fps").
//
//  What the game shows and draws, measured while it runs at full speed. Four rows under the
//  pause meter (top left of the picture), 128 pixels wide:
//    D xxxxxx  frame buffer of the displayed picture: the current-line address in use at the
//              middle of the picture, i.e. the last one a display list entry loaded (CLUT DMA,
//              LDCURR) before the end of active line 120 (a game may show a small band from
//              another buffer at the top: Strahl's first entry points near the end of the VRAM)
//    C xxxxxx  frame buffer the cel engine draws into (REGCTL3) at the start of its last cel
//              list of the field
//              Colour of both addresses: green if the whole 320x240 16-bit screen from there is
//              in FPGA memory (the BRAM blocks of the VRAM, never a pause), yellow if part of it
//              is in DDR3, red if all of it is (pauses while the video reads it), grey outside
//              the VRAM (DRAM).
//    bar       share of the field the cel engine runs (SPRON, memory pauses included):
//              128 pixels = 100 %, grey marks every 25 %, white mark: highest of the last 64 fields
//    F nn      new pictures per second: fields whose displayed frame buffer differs from the
//              previous field's, counted over the last second (60 NTSC / 50 PAL fields)
//============================================================================
module P3DO_DEBUG_INFO
(
	input             CLK,          // clk_sys
	input             PAL,
	input             SPRON,        // the cel engine runs
	input      [23:0] WBASE,        // its frame buffer (REGCTL3)
	input      [23:0] VCURR,        // current-line address of a display list entry that loads one
	input             VCURR_STB,    //   one clock when it is read
	input       [3:0] BRAM4_OFFS,   // VRAM blocks kept in FPGA memory (3DO.sv)
	input       [3:0] BRAM2_OFFS,
	input       [3:0] BRAM1_OFFS,
	input             BRAM_BLOCKS,  // USE_BRAM_BLOCKS_FOR_VRAM
	input             ENABLE,       // OSD option (CLK domain)

	input             VCLK,         // video clock
	input             CE_PIX,
	input             HBL_N,
	input             VBL_N,
	input      [23:0] RGB_IN,
	output     [23:0] RGB_OUT       // RGB_IN unchanged when the option is off
);

	//------------------------------------------------------------------------
	// Measure (CLK domain)
	//------------------------------------------------------------------------
	reg  [ 2:0] vbl_sync;
	always @(posedge CLK) vbl_sync <= {vbl_sync[1:0], VBL_N};
	wire        frame_end = vbl_sync[2] & ~vbl_sync[1];	// start of the vertical blank

	// 16 KB pieces of the VRAM in FPGA memory: piece = address[19:14]
	function automatic in_bram(input [5:0] p, input [3:0] b4, input [3:0] b2, input [3:0] b1, input blocks);
		reg [4:0] blk;
		begin
			blk = {1'b0, p[5:2]};
			in_bram = (blocks && blk >= {1'b0, b4} && blk <= {1'b0, b4} + 5'd3) ||
			          (blocks && blk >= {1'b0, b2} && blk <= {1'b0, b2} + 5'd1) ||
			          (p[5:2] == b1 && p[1:0] != 2'b11) || p == 6'd63;
		end
	endfunction

	reg  [ 2:0] hbl_sync;
	reg  [ 7:0] line_cnt;      // active lines ended in the field in progress
	reg         spron_old;
	reg  [23:0] draw_last;     // REGCTL3 at the start of the last cel list
	reg  [23:0] vcurr_last;    // last display list address loaded
	reg  [23:0] disp_cur;      // display list address in use at the middle of the field in progress
	reg         disp_got;
	reg  [23:0] disp, draw;    // results of the last field
	reg  [ 5:0] field_n, new_n;
	reg  [ 5:0] fps;
	reg  [23:0] frame_cnt, busy_cnt, snap_frame, snap_busy;

	// coverage of the two screens, one 16 KB piece per clock: 0 grey, 1 green, 2 yellow, 3 red
	reg  [ 1:0] cov_d, cov_c;
	reg  [ 1:0] cov_st;        // 0 idle, 1 displayed screen, 2 drawn screen
	reg  [ 5:0] cov_p, cov_last;
	reg  [ 3:0] cov_in, cov_tot;
	reg  [23:0] cov_end;

	// bar = 256 x busy / frame, one quotient bit per clock (as the pause meter)
	reg  [ 3:0] div_bit;
	reg  [23:0] div_rem;
	reg  [ 7:0] div_q;
	reg  [ 7:0] bar, peak;
	reg  [ 5:0] peak_age;
	wire [24:0] div_rem2 = {div_rem, 1'b0};
	wire [24:0] div_sub = div_rem2 - {1'b0, snap_frame};
	reg         shadow_tgl;

	always @(posedge CLK) begin
		spron_old <= SPRON;
		if (SPRON && !spron_old) draw_last <= WBASE;
		hbl_sync <= {hbl_sync[1:0], HBL_N};
		if (VCURR_STB) vcurr_last <= VCURR;
		if (vbl_sync[1] && !vbl_sync[2]) line_cnt <= 8'd0;	// start of the active picture
		else if (vbl_sync[1] && hbl_sync[2] && !hbl_sync[1] && ~&line_cnt) begin	// end of an active line
			line_cnt <= line_cnt + 8'd1;
			if (line_cnt == 8'd119 && !disp_got) begin	// end of line 120: the middle of the picture
				disp_cur <= VCURR_STB ? VCURR : vcurr_last;
				disp_got <= 1'b1;
			end
		end

		if (frame_end) begin
			// the field that ends: displayed and drawn screens, new picture or not
			disp <= disp_got ? disp_cur : disp;
			draw <= draw_last;
			disp_got <= 1'b0;
			if (field_n == (PAL ? 6'd49 : 6'd59)) begin
				fps <= new_n + ((disp_got ? disp_cur : disp) != disp ? 6'd1 : 6'd0);
				field_n <= 6'd0;
				new_n <= 6'd0;
			end
			else begin
				field_n <= field_n + 6'd1;
				if ((disp_got ? disp_cur : disp) != disp && ~&new_n) new_n <= new_n + 6'd1;
			end
			snap_frame <= frame_cnt;
			snap_busy <= busy_cnt;
			frame_cnt <= 24'd1;
			busy_cnt <= {23'd0, SPRON};
			div_bit <= 4'd1;
			cov_st <= 2'd1;
			cov_p <= (disp_got ? disp_cur[19:14] : disp[19:14]);
			cov_end <= (disp_got ? disp_cur : disp) + 24'h0257FF;
			cov_in <= 4'd0;
			cov_tot <= 4'd0;
		end
		else begin
			if (~&frame_cnt) frame_cnt <= frame_cnt + 24'd1;
			if (SPRON && ~&busy_cnt) busy_cnt <= busy_cnt + 24'd1;

			// engine bar
			if (div_bit == 4'd1) begin
				div_rem <= snap_busy;
				div_q <= '0;
				div_bit <= 4'd2;
			end
			else if (div_bit >= 4'd2 && div_bit <= 4'd9) begin
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
			else if (div_bit == 4'd10) begin
				bar <= (snap_busy >= snap_frame && snap_frame != 0) ? 8'd255 : div_q;
				div_bit <= 4'd0;
			end

			// coverage: pieces from the start of the screen to its last byte (capped at the end of the VRAM)
			if (cov_st != 2'd0) begin
				if (cov_tot == 4'd0) cov_last <= cov_end[23:20] != 4'h2 ? 6'd63 : cov_end[19:14];
				if ((cov_st == 2'd1 ? disp[23:20] : draw[23:20]) != 4'h2) begin
					if (cov_st == 2'd1) cov_d <= 2'd0; else cov_c <= 2'd0;
					cov_tot <= 4'd15;	// done
				end
				else if (cov_tot != 4'd15) begin
					cov_in <= cov_in + (in_bram(cov_p, BRAM4_OFFS, BRAM2_OFFS, BRAM1_OFFS, BRAM_BLOCKS) ? 4'd1 : 4'd0);
					cov_tot <= cov_tot + 4'd1;
					cov_p <= cov_p + 6'd1;
					if (cov_tot != 4'd0 && (cov_p == cov_last + 6'd1 || cov_tot == 4'd12)) begin
						// cov_p ran one past the last piece: the counts are complete
						if (cov_st == 2'd1) cov_d <= cov_in == cov_tot ? 2'd1 : cov_in == 4'd0 ? 2'd3 : 2'd2;
						else cov_c <= cov_in == cov_tot ? 2'd1 : cov_in == 4'd0 ? 2'd3 : 2'd2;
						cov_tot <= 4'd15;
					end
				end
				if (cov_tot == 4'd15) begin
					if (cov_st == 2'd1) begin
						cov_st <= 2'd2;
						cov_p <= draw[19:14];
						cov_end <= draw + 24'h0257FF;
						cov_in <= 4'd0;
						cov_tot <= 4'd0;
					end
					else begin
						cov_st <= 2'd0;
						peak_age <= peak_age + 6'd1;
						if (bar > peak || peak_age == 6'd63) peak <= bar;
						shadow_tgl <= ~shadow_tgl;
					end
				end
			end
		end
	end

	//------------------------------------------------------------------------
	// Draw (VCLK domain)
	//------------------------------------------------------------------------
	reg  [ 2:0] tgl_sync, en_sync;
	reg  [23:0] v_disp, v_draw;
	reg  [ 1:0] v_cov_d, v_cov_c;
	reg  [ 7:0] v_bar, v_peak;
	reg  [ 3:0] v_tens, v_ones;
	always @(posedge VCLK) begin
		tgl_sync <= {tgl_sync[1:0], shadow_tgl};
		en_sync <= {en_sync[1:0], ENABLE};
		if (tgl_sync[2] != tgl_sync[1]) begin	// the results are stable for a whole field after the toggle
			v_disp <= disp;
			v_draw <= draw;
			v_cov_d <= cov_d;
			v_cov_c <= cov_c;
			v_bar <= bar;
			v_peak <= peak;
			v_tens <= fps >= 6'd60 ? 4'd6 : fps >= 6'd50 ? 4'd5 : fps >= 6'd40 ? 4'd4 : fps >= 6'd30 ? 4'd3 :
			          fps >= 6'd20 ? 4'd2 : fps >= 6'd10 ? 4'd1 : 4'd0;
			v_ones <= fps >= 6'd60 ? 4'(fps - 6'd60) : fps >= 6'd50 ? 4'(fps - 6'd50) : fps >= 6'd40 ? 4'(fps - 6'd40) :
			          fps >= 6'd30 ? 4'(fps - 6'd30) : fps >= 6'd20 ? 4'(fps - 6'd20) : fps >= 6'd10 ? 4'(fps - 6'd10) : fps[3:0];
		end
	end

	// Rows of 8 lines from line Y0, 8 cells of 16 pixels from pixel X0, on a dark box (below the pause meter)
	localparam [9:0] X0 = 10'd16;
	localparam [9:0] Y0 = 10'd72;

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

	wire [ 9:0] rx = x - X0;
	wire [ 9:0] ry = y - Y0;
	wire [ 1:0] row = ry[4:3];
	wire [ 2:0] gy = ry[2:0];
	wire [ 2:0] cpos = rx[6:4];
	wire [ 2:0] gx = rx[3:1];
	wire        in_box = x + 10'd1 >= X0 && x <= X0 + 10'd128 && y + 10'd1 >= Y0 && y < Y0 + 10'd32;
	wire        in_area = x >= X0 && x < X0 + 10'd128 && y >= Y0 && y < Y0 + 10'd32;

	// 5 x 7 hexadecimal digits, each row 5 bits, left pixel in bit 4
	function automatic [4:0] glyph(input [3:0] c, input [2:0] r);
		reg [34:0] g;
		begin
			case (c)
				4'h0: g = 35'b01110_10001_10011_10101_11001_10001_01110;
				4'h1: g = 35'b00100_01100_00100_00100_00100_00100_01110;
				4'h2: g = 35'b01110_10001_00001_00010_00100_01000_11111;
				4'h3: g = 35'b11111_00010_00100_00010_00001_10001_01110;
				4'h4: g = 35'b00010_00110_01010_10010_11111_00010_00010;
				4'h5: g = 35'b11111_10000_11110_00001_00001_10001_01110;
				4'h6: g = 35'b00110_01000_10000_11110_10001_10001_01110;
				4'h7: g = 35'b11111_00001_00010_00100_01000_01000_01000;
				4'h8: g = 35'b01110_10001_10001_01110_10001_10001_01110;
				4'h9: g = 35'b01110_10001_10001_01111_00001_00010_01100;
				4'hA: g = 35'b01110_10001_10001_11111_10001_10001_10001;
				4'hB: g = 35'b11110_10001_10001_11110_10001_10001_11110;
				4'hC: g = 35'b01110_10001_10000_10000_10000_10001_01110;
				4'hD: g = 35'b11110_10001_10001_10001_10001_10001_11110;
				4'hE: g = 35'b11111_10000_10000_11110_10000_10000_11111;
				default: g = 35'b11111_10000_10000_11110_10000_10000_10000;
			endcase
			glyph = g[34 - 5 * r -: 5];
		end
	endfunction

	function automatic [23:0] cov_color(input [1:0] c);
		case (c)
			2'd1: cov_color = 24'h40FF40;	// all in FPGA memory
			2'd2: cov_color = 24'hFFD000;	// partly in DDR3
			2'd3: cov_color = 24'hFF4040;	// all in DDR3
			default: cov_color = 24'hB0B0B0;	// outside the VRAM
		endcase
	endfunction

	// character of the cpos: label, blank or digit
	reg  [ 3:0] ch;
	reg         ch_on;
	reg  [23:0] ch_color;
	reg  [23:0] addr;
	always_comb begin
		addr = row == 2'd0 ? v_disp : v_draw;
		ch = 4'h0;
		ch_on = 1'b0;
		ch_color = 24'hFFFFFF;
		case (row)
			2'd0, 2'd1: begin
				if (cpos == 3'd0) begin
					ch = row == 2'd0 ? 4'hD : 4'hC;
					ch_on = 1'b1;
				end
				else if (cpos >= 3'd2) begin
					ch = addr[4 * (7 - cpos) +: 4];
					ch_on = 1'b1;
					ch_color = cov_color(row == 2'd0 ? v_cov_d : v_cov_c);
				end
			end
			2'd3: begin
				if (cpos == 3'd0) begin ch = 4'hF; ch_on = 1'b1; end
				else if (cpos == 3'd2) begin ch = v_tens; ch_on = 1'b1; end
				else if (cpos == 3'd3) begin ch = v_ones; ch_on = 1'b1; end
			end
			default: ;
		endcase
	end

	wire [ 4:0] gbits = glyph(ch, gy);
	wire        pix_on = ch_on && gy < 3'd7 && gx < 3'd5 && gbits[3'd4 - gx];
	wire [ 7:0] bx = {1'b0, rx[6:0]};	// position in the bar, 0..127
	reg  [23:0] ovl;
	always_comb begin
		ovl = 24'h202020;
		if (in_area) begin
			if (row == 2'd2) begin
				if (gy < 3'd6) begin
					if (bx == {1'b0, v_peak[7:1]} && v_peak != 8'd0) ovl = 24'hFFFFFF;
					else if (bx < {1'b0, v_bar[7:1]}) ovl = 24'hFF8000;
					else if (bx[4:0] == 5'd31) ovl = 24'h707070;
				end
			end
			else if (pix_on) ovl = ch_color;
		end
	end
	assign RGB_OUT = en_sync[2] && in_box ? ovl : RGB_IN;

endmodule
