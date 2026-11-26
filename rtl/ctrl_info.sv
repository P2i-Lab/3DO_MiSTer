//============================================================================
//  Controller box (Input page option "Show controllers"), 2026-11-24.
//
//  A box at the top left of the picture with one line per MiSTer player and what it does in the 3DO, e.g.
//    Player 1  Gamepad 1
//    Player 2  Light gun 1
//    Player 3  Not used
//    Player 4  Not used
//    Mouse     Not used
//  MiSTer numbers the controllers in the order of their first button press ("Reset player assignment" on its System
//  page starts again); the mouse never gets a number. A line turns yellow with a star while that controller has a button
//  or a direction pressed (the mouse line also for half a second after it moves). Players 5 and 6 are listed only when
//  they do something, the SNAC line only with GunCons on the SNAC port.
//  Text: cells of 12 pixels (5 dots of 2 pixels, the picture lines are 640 pixels), rows of 10 lines (8 dot lines).
//============================================================================
module P3DO_CTRL_INFO
(
	input             CLK,          // clk_sys
	input             ENABLE,       // OSD option
	input      [ 2:0] PADS,         // number of 3DO pads: players 1..PADS
	input      [ 2:0] GUN_MODE,     // 0 off, 1 mouse, 2 USB gun, 3 2 USB guns, 4 USB gun + mouse, 5 SNAC GunCon, 6 2 SNAC GunCons, 7 SNAC Saturn gun
	input             STICK,        // flightstick on: the player after the pads
	input             MOUSE3DO,     // 3DO mouse on
	input             ARCADE,       // arcade control: trackball on the mouse, start and coin on players 1 and 2
	input      [12:0] JOY0,         // directions and buttons of players 1-6
	input      [12:0] JOY1,
	input      [12:0] JOY2,
	input      [12:0] JOY3,
	input      [12:0] JOY4,
	input      [12:0] JOY5,
	input      [24:0] PS2_MOUSE,
	input      [ 5:0] SNAC_BTN,     // buttons of the guns on the SNAC port
	input             VBL_N,

	input             VCLK,         // video clock
	input             CE_PIX,
	input             HBL_N,
	input      [23:0] RGB_IN,
	output     [23:0] RGB_OUT       // RGB_IN unchanged when the option is off
);

	//------------------------------------------------------------------------
	// What each controller does (CLK domain)
	//------------------------------------------------------------------------
	localparam [3:0] R_NONE = 4'd0, R_GUN1 = 4'd5, R_GUN2 = 4'd6, R_STICK = 4'd7, R_M3DO = 4'd8, R_TBALL = 4'd9,
	                 R_GUN12 = 4'd10, R_ARC1 = 4'd11, R_ARC2 = 4'd12;	// 1-4: Gamepad 1-4

	reg  [ 2:0] vbl_s;
	reg         mtgl;
	reg  [ 4:0] mhold;
	always @(posedge CLK) begin
		vbl_s <= {vbl_s[1:0], VBL_N};
		mtgl <= PS2_MOUSE[24];
		if (mtgl != PS2_MOUSE[24]) mhold <= 5'd30;	// the mouse moved or clicked: lit for half a second
		else if (vbl_s[2] && !vbl_s[1] && mhold != 5'd0) mhold <= mhold - 5'd1;
	end

	wire        gun1_usb = GUN_MODE == 3'd2 || GUN_MODE == 3'd3 || GUN_MODE == 3'd4;
	wire        gun2_usb = GUN_MODE == 3'd3;

	function automatic [3:0] player_role(input [2:0] p, input [2:0] pads, input arcade, input g1, input g2, input stick);
		begin
			if (arcade)                      player_role = p == 3'd0 ? R_ARC1 : p == 3'd1 ? R_ARC2 : R_NONE;
			else if (p < pads)               player_role = {1'b0, p} + 4'd1;
			else if (g1 && p == pads)        player_role = R_GUN1;
			else if (g2 && p == pads + 3'd1) player_role = R_GUN2;
			else if (stick && p == pads)     player_role = R_STICK;
			else                             player_role = R_NONE;
		end
	endfunction

	// candidates: players 1-6, mouse, SNAC; 4 bits of role each
	reg  [31:0] c_roles;
	reg  [ 7:0] c_lit;
	always @(posedge CLK) begin
		c_roles[ 3: 0] <= player_role(3'd0, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[ 7: 4] <= player_role(3'd1, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[11: 8] <= player_role(3'd2, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[15:12] <= player_role(3'd3, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[19:16] <= player_role(3'd4, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[23:20] <= player_role(3'd5, PADS, ARCADE, gun1_usb, gun2_usb, STICK);
		c_roles[27:24] <= ARCADE ? R_TBALL : GUN_MODE == 3'd1 ? R_GUN1 : GUN_MODE == 3'd4 ? R_GUN2 : MOUSE3DO ? R_M3DO : R_NONE;
		c_roles[31:28] <= GUN_MODE == 3'd6 ? R_GUN12 : R_GUN1;
		c_lit <= {|SNAC_BTN, |PS2_MOUSE[2:0] || mhold != 5'd0, |JOY5, |JOY4, |JOY3, |JOY2, |JOY1, |JOY0};
	end

	// rows of the box: players 1-4, then player 5 and 6 when they do something, the mouse, the SNAC GunCons
	// (no SNAC GunCon gives a role to player 5: at most 4 pads, the USB guns are off)
	wire        v5 = c_roles[19:16] != R_NONE;
	wire        v6 = c_roles[23:20] != R_NONE;
	wire        vs = GUN_MODE == 3'd5 || GUN_MODE == 3'd6 || GUN_MODE == 3'd7;
	wire [ 3:0] i4 = v5 ? 4'd4 : 4'd6;	// candidate of rows 4-6, 8: no row
	wire [ 3:0] i5 = v6 ? 4'd5 : v5 ? 4'd6 : vs ? 4'd7 : 4'd8;
	wire [ 3:0] i6 = v6 ? 4'd6 : 4'd8;
	wire [ 2:0] n_rows = v6 ? 3'd7 : (v5 || vs) ? 3'd6 : 3'd5;

	function automatic [7:0] row_of(input [3:0] i, input [31:0] roles, input [7:0] lit);	// label, role, lit
		row_of = i[3] ? 8'd0 : {i[2:0], roles[4 * i[2:0] +: 4], lit[i[2:0]]};
	endfunction

	reg  [55:0] d_rows;
	reg  [ 2:0] d_n;
	reg         d_en;
	always @(posedge CLK) begin
		d_rows <= {row_of(i6, c_roles, c_lit), row_of(i5, c_roles, c_lit), row_of(i4, c_roles, c_lit),
		           row_of(4'd3, c_roles, c_lit), row_of(4'd2, c_roles, c_lit), row_of(4'd1, c_roles, c_lit),
		           row_of(4'd0, c_roles, c_lit)};
		d_n <= n_rows;
		d_en <= ENABLE;
	end

	//------------------------------------------------------------------------
	// Draw (VCLK domain; the values change seldom, two flip-flops each)
	//------------------------------------------------------------------------
	reg  [55:0] v_rows1, v_rows;
	reg  [ 2:0] v_n1, v_n;
	reg  [ 1:0] v_en;
	always @(posedge VCLK) begin
		v_rows1 <= d_rows;
		v_rows <= v_rows1;
		v_n1 <= d_n;
		v_n <= v_n1;
		v_en <= {v_en[0], d_en};
	end

	localparam [9:0] X0 = 10'd24;	// first text pixel
	localparam [9:0] Y0 = 10'd12;	// first text line

	reg  [ 9:0] x;
	reg  [ 9:0] y;
	reg         hbl_old;
	reg  [ 3:0] px;	// pixel in the cell, 0-11
	reg  [ 4:0] col;	// cell
	reg  [ 3:0] ln;	// line in the row, 0-9
	reg  [ 2:0] row;	// row
	always @(posedge VCLK) begin
		if (CE_PIX) begin
			hbl_old <= HBL_N;
			if (!HBL_N) begin
				x <= '0;
				px <= '0;
				col <= '0;
			end
			else begin
				if (~&x) x <= x + 10'd1;
				if (x >= X0) begin
					if (px == 4'd11) begin
						px <= '0;
						if (~&col) col <= col + 5'd1;
					end
					else px <= px + 4'd1;
				end
			end
			if (!VBL_N) begin
				y <= '0;
				ln <= '0;
				row <= '0;
			end
			else if (hbl_old && !HBL_N) begin	// end of an active line
				if (~&y) y <= y + 10'd1;
				if (y >= Y0) begin
					if (ln == 4'd9) begin
						ln <= '0;
						if (~&row) row <= row + 3'd1;
					end
					else ln <= ln + 4'd1;
				end
			end
		end
	end

	function automatic [7:0] label_char(input [2:0] l, input [2:0] i);	// 8 characters
		reg [63:0] s;
		begin
			case (l)
				3'd0:    s = "Player 1";
				3'd1:    s = "Player 2";
				3'd2:    s = "Player 3";
				3'd3:    s = "Player 4";
				3'd4:    s = "Player 5";
				3'd5:    s = "Player 6";
				3'd6:    s = "Mouse   ";
				default: s = "SNAC    ";
			endcase
			label_char = s[63 - 8 * i -: 8];
		end
	endfunction

	function automatic [7:0] role_char(input [3:0] r, input [3:0] i);	// 13 characters
		reg [103:0] s;
		begin
			case (r)
				4'd1:    s = "Gamepad 1    ";
				4'd2:    s = "Gamepad 2    ";
				4'd3:    s = "Gamepad 3    ";
				4'd4:    s = "Gamepad 4    ";
				4'd5:    s = "Light gun 1  ";
				4'd6:    s = "Light gun 2  ";
				4'd7:    s = "Flightstick  ";
				4'd8:    s = "3DO mouse    ";
				4'd9:    s = "Trackball    ";
				4'd10:   s = "Light gun 1+2";
				4'd11:   s = "Arcade 1     ";
				4'd12:   s = "Arcade 2     ";
				default: s = "Not used     ";
			endcase
			role_char = s[103 - 8 * i -: 8];
		end
	endfunction

	// 5 x 8 dots, row 0 first, left dot in the high bit of each row; row 7 for g, p, y
	function automatic [39:0] glyph(input [7:0] c);
		case (c)
			"1": glyph = 40'b00100_01100_00100_00100_00100_00100_01110_00000;
			"2": glyph = 40'b01110_10001_00001_00010_00100_01000_11111_00000;
			"3": glyph = 40'b11111_00010_00100_00010_00001_10001_01110_00000;
			"4": glyph = 40'b00010_00110_01010_10010_11111_00010_00010_00000;
			"5": glyph = 40'b11111_10000_11110_00001_00001_10001_01110_00000;
			"6": glyph = 40'b00110_01000_10000_11110_10001_10001_01110_00000;
			"A": glyph = 40'b01110_10001_10001_11111_10001_10001_10001_00000;
			"C": glyph = 40'b01110_10001_10000_10000_10000_10001_01110_00000;
			"D": glyph = 40'b11110_10001_10001_10001_10001_10001_11110_00000;
			"F": glyph = 40'b11111_10000_10000_11110_10000_10000_10000_00000;
			"G": glyph = 40'b01110_10001_10000_10111_10001_10001_01111_00000;
			"L": glyph = 40'b10000_10000_10000_10000_10000_10000_11111_00000;
			"M": glyph = 40'b10001_11011_10101_10101_10001_10001_10001_00000;
			"N": glyph = 40'b10001_10001_11001_10101_10011_10001_10001_00000;
			"O": glyph = 40'b01110_10001_10001_10001_10001_10001_01110_00000;
			"P": glyph = 40'b11110_10001_10001_11110_10000_10000_10000_00000;
			"S": glyph = 40'b01111_10000_10000_01110_00001_00001_11110_00000;
			"T": glyph = 40'b11111_00100_00100_00100_00100_00100_00100_00000;
			"a": glyph = 40'b00000_00000_01110_00001_01111_10001_01111_00000;
			"b": glyph = 40'b10000_10000_10110_11001_10001_10001_11110_00000;
			"c": glyph = 40'b00000_00000_01110_10000_10000_10001_01110_00000;
			"d": glyph = 40'b00001_00001_01101_10011_10001_10001_01111_00000;
			"e": glyph = 40'b00000_00000_01110_10001_11111_10000_01110_00000;
			"g": glyph = 40'b00000_00000_01111_10001_10001_01111_00001_01110;
			"h": glyph = 40'b10000_10000_10110_11001_10001_10001_10001_00000;
			"i": glyph = 40'b00100_00000_01100_00100_00100_00100_01110_00000;
			"k": glyph = 40'b10000_10000_10010_10100_11000_10100_10010_00000;
			"l": glyph = 40'b01100_00100_00100_00100_00100_00100_01110_00000;
			"m": glyph = 40'b00000_00000_11010_10101_10101_10001_10001_00000;
			"n": glyph = 40'b00000_00000_10110_11001_10001_10001_10001_00000;
			"o": glyph = 40'b00000_00000_01110_10001_10001_10001_01110_00000;
			"p": glyph = 40'b00000_00000_11110_10001_10001_11110_10000_10000;
			"r": glyph = 40'b00000_00000_10110_11001_10000_10000_10000_00000;
			"s": glyph = 40'b00000_00000_01111_10000_01110_00001_11110_00000;
			"t": glyph = 40'b01000_01000_11100_01000_01000_01001_00110_00000;
			"u": glyph = 40'b00000_00000_10001_10001_10001_10011_01101_00000;
			"y": glyph = 40'b00000_00000_10001_10001_10001_01111_00001_01110;
			"*": glyph = 40'b00000_00100_10101_01110_10101_00100_00000_00000;
			"+": glyph = 40'b00000_00100_00100_11111_00100_00100_00000_00000;
			default: glyph = 40'd0;
		endcase
	endfunction

	// text columns: 0-7 label, 8-9 blank, 10-22 role, 23 blank, 24 star while the controller is used
	wire [ 7:0] rd = v_rows[8 * row +: 8];	// label, role, lit of the row
	reg  [ 7:0] ch_next;
	always_comb begin
		if (col < 5'd8)                       ch_next = label_char(rd[7:5], col[2:0]);
		else if (col >= 5'd10 && col < 5'd23) ch_next = role_char(rd[4:1], 4'(col - 5'd10));
		else if (col == 5'd24 && rd[0])       ch_next = "*";
		else                                  ch_next = " ";
	end
	reg  [ 7:0] ch;	// character of the cell, from its second pixel
	always @(posedge VCLK) if (CE_PIX && px == 4'd0) ch <= ch_next;

	wire [39:0] gbits = glyph(ch);
	wire [ 3:0] gx = px - 4'd1;	// 0-9 over the 5 dots
	wire        dot = ln < 4'd8 && px != 4'd0 && px <= 4'd10 && gbits[6'd39 - 6'd5 * {3'd0, ln[2:0]} - {3'd0, gx[3:1]}];
	wire [ 9:0] rows_h = {4'd0, v_n, 3'd0} + {6'd0, v_n, 1'b0};	// 10 lines a row
	wire        in_text = x >= X0 && x < X0 + 10'd300 && y >= Y0 && y < Y0 + rows_h;
	wire        in_box = x + 10'd8 >= X0 && x < X0 + 10'd308 && y + 10'd4 >= Y0 && y < Y0 + rows_h + 10'd3;
	wire [23:0] fg = rd[0] ? 24'hFFE040 : col < 5'd8 ? 24'h90C8FF : rd[4:1] == R_NONE ? 24'h808080 : 24'hFFFFFF;
	assign RGB_OUT = v_en[1] && in_box ? (in_text && dot ? fg : 24'h141414) : RGB_IN;

endmodule
