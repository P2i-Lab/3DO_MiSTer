//============================================================================
//  Light guns of the 3DO (control port device 0x4D) and their crosshairs.
//
//  The real gun, measured on an FZ-10 with two guns (bench disc 3.4, pages PISTOLET CIBLES,
//  PISTOLETS 2 JOUEURS and PORT MANETTE DIRECT), sends 5 bytes on the control port:
//    4D, its buttons (80 trigger, 48 calibration button), then a 20-bit counter and the
//    number of lines it saw: byte 1 bit 0 = counter[19], byte 2 = counter[18:11],
//    byte 3 = counter[10:3], byte 4 = counter[2:0] and the 5-bit line count.
//  The counter counts at about 20 MHz from the start of the field until the gun first sees
//  the beam: 1270.4 per line (disc 3.5 PISTOLET BALAYAGE, a bar swept under a gun at rest:
//  1270.1, 1270.4, 1270.6 in three runs) and 3.19 per 3DO pixel along a line (1.595 per dot
//  of the 640-dot line); it is always even. Over 36 aimed shots the first line seen was the
//  aimed line + 11 on average (a gun sees 20 to 31 lines of a white screen, the first one
//  about half of them above the aim); the offset below keeps that fit on the middle line
//  (120), where the earlier 1271.1 per line gave the same counter. The gun measures at every
//  field, trigger pressed or not;
//  when it sees nothing (aimed off the picture, at the black border or at a dark area) the
//  counter and the line count are 0.
//  Mad Dog McCree (American Laser Games) reads the counter the same way before its
//  calibration: 1271.0 per line and 3.20 per pixel (counter 0 at the top left corner; the
//  calibration shot then sets the offset).
//  With the ideal sensor the emulated gun sees 24 lines wherever it is aimed on the picture.
//  With the picture sensor it sees, as the real one, only the lit places of the picture within
//  its field (a disc 25 lines across, P3DO_GUN_VIDEO): the counter is the time of the first
//  lit place, the line count the lines with a lit place. Aimed off the picture, or with the
//  reload button held (an off-screen shot: trigger pressed and nothing seen, which is how the
//  games reload), it sees nothing.
//  PAL is not measured: same counter on the line length (784 dots) and the first picture
//  line (45) of the core's PAL video.
//
//  Aim: the left stick of a joystick (the light guns that MiSTer sees as joysticks give their
//  position there, -128..127 across the picture; the extreme values are taken as off the
//  picture), the mouse (moves the aim, which can leave the picture by a margin), or a gun
//  that times the beam itself (GunCon on the SNAC port, P3DO_GUNCON: it sees nothing where
//  the picture is dark, as the real gun).
//  Buttons: trigger A / left, reload B / right, calibration button C / middle.
//============================================================================
module P3DO_LIGHTGUN
(
	input             CLK,
	input             RST,
	input             PAL,
	input             MOUSE_MODE,   // 0: aimed with the joystick, 1: with the mouse
	input             EXT_MODE,     // 1: aimed by an outside gun (EXT_*), over MOUSE_MODE

	input      [ 7:0] JOY_X,        // left stick, signed: -128 left .. 127 right
	input      [ 7:0] JOY_Y,        //                     -128 top  .. 127 bottom
	input             JOY_TRIGGER,
	input             JOY_RELOAD,
	input             JOY_CALIB,
	input      [24:0] PS2_MOUSE,
	input      [ 9:0] EXT_X,        // dot, line, on the picture
	input      [ 7:0] EXT_Y,
	input             EXT_ON,
	input             EXT_TRIGGER,
	input             EXT_RELOAD,
	input             EXT_CALIB,

	input             SENSE,        // 1: the gun sees only the lit places of the picture (LIT_*), 0: ideal
	input             LIT,          // the field of the gun had a lit place in the last field
	input      [ 9:0] LIT_X,        //   first lit place: dot, line
	input      [ 7:0] LIT_Y,
	input      [ 4:0] LIT_N,        //   lines with a lit place

	output reg [39:0] DATA,         // record sent on the control port, first bit first
	output reg [ 9:0] AIM_X,        // aim: dot of the active line (0-639)
	output reg [ 7:0] AIM_Y,        //      active line (0-239)
	output reg        AIM_ON        //      on the picture
);

	// Joystick: 256 steps across the 640 dots and the 240 lines
	wire [ 7:0] jx = JOY_X ^ 8'h80;
	wire [ 7:0] jy = JOY_Y ^ 8'h80;
	wire [10:0] jx5 = {1'b0, jx, 2'b00} + {3'b000, jx};	// x 5, then / 2
	wire [11:0] jy15 = {jy, 4'b0000} - {4'b0000, jy};	// x 15, then / 16
	wire        joy_off = jx <= 8'd1 || jx == 8'd255 || jy <= 8'd1 || jy == 8'd255;

	// Mouse: one count moves the aim by one 3DO pixel (2 dots) or one line; the aim can go
	// 32 pixels and 32 lines beyond the picture
	localparam signed [12:0] MX_MIN = -13'sd64, MX_MAX = 13'sd703;
	localparam signed [11:0] MY_MIN = -12'sd32, MY_MAX = 12'sd271;
	reg  signed [11:0] mx;
	reg  signed [10:0] my;
	reg         m_toggle, m_left, m_right, m_middle;
	wire signed [ 9:0] mdx = PS2_MOUSE[6] ? (PS2_MOUSE[4] ? -10'sd256 : 10'sd255) : $signed({PS2_MOUSE[4], PS2_MOUSE[4], PS2_MOUSE[15:8]});
	wire signed [ 9:0] mdy = PS2_MOUSE[7] ? (PS2_MOUSE[5] ? -10'sd256 : 10'sd255) : $signed({PS2_MOUSE[5], PS2_MOUSE[5], PS2_MOUSE[23:16]});
	wire signed [12:0] mx_new = $signed({mx[11], mx}) + $signed({{2{mdx[9]}}, mdx, 1'b0});
	wire signed [11:0] my_new = $signed({my[10], my}) - $signed({{2{mdy[9]}}, mdy});

	always @(posedge CLK) begin
		if (RST || !MOUSE_MODE) begin
			mx <= 12'sd320;
			my <= 11'sd120;
			m_toggle <= PS2_MOUSE[24];
			m_left <= 1'b0;
			m_right <= 1'b0;
			m_middle <= 1'b0;
		end
		else if (PS2_MOUSE[24] != m_toggle) begin
			m_toggle <= PS2_MOUSE[24];
			m_left <= PS2_MOUSE[0];
			m_right <= PS2_MOUSE[1];
			m_middle <= PS2_MOUSE[2];
			mx <= mx_new < MX_MIN ? MX_MIN[11:0] : mx_new > MX_MAX ? MX_MAX[11:0] : mx_new[11:0];
			my <= my_new < MY_MIN ? MY_MIN[10:0] : my_new > MY_MAX ? MY_MAX[10:0] : my_new[10:0];
		end
	end
	wire        m_on = mx >= 0 && mx <= 12'sd639 && my >= 0 && my <= 11'sd239;

	reg         trigger, reload, calib;
	always @(posedge CLK) begin
		if (EXT_MODE) begin
			AIM_X <= EXT_X;
			AIM_Y <= EXT_Y;
			AIM_ON <= EXT_ON;
			trigger <= EXT_TRIGGER;
			reload <= EXT_RELOAD;
			calib <= EXT_CALIB;
		end
		else if (MOUSE_MODE) begin
			AIM_X <= mx[9:0];
			AIM_Y <= my[7:0];
			AIM_ON <= m_on;
			trigger <= m_left;
			reload <= m_right;
			calib <= m_middle;
		end
		else begin
			AIM_X <= jx5[10:1];
			AIM_Y <= jy15[11:4];
			AIM_ON <= !joy_off;
			trigger <= JOY_TRIGGER;
			reload <= JOY_RELOAD;
			calib <= JOY_CALIB;
		end
	end

	// Ideal sensor: the gun sees the picture wherever it is aimed; its first line is 12 above the aim:
	//   counter = 1270.4 x (line + 11) + 236.1 + 1.595 x dot, to the nearest even number (sums x 256).
	//   1270.4 counts per line and 3.19 per pixel were measured on an FZ-10 with a real gun at rest
	//   (disc 3.5 PISTOLET BALAYAGE, three runs: 1270.1, 1270.4, 1270.6 from the top and bottom edges of
	//   the gun's spot); the offset keeps the counter of the picture's middle line (120) where it was with
	//   the earlier 1271.1 x (line + 11) + 144, fitted on aimed shots (disc 3.4 PISTOLET CIBLES).
	//   PAL (not measured): 1277.6 per line of 784 dots, line + 34, + 144.
	// Picture sensor (SENSE): the first lit place in the field of the gun, found by P3DO_GUN_VIDEO in the
	// picture (LIT_X, LIT_Y, LIT_N lines lit): counter = 1270.4 x (first line + 23) + 236.1 + 1.595 x dot.
	wire [ 8:0] c_line = SENSE ? {1'b0, LIT_Y} + (PAL ? 9'd46 : 9'd23) : {1'b0, AIM_Y} + (PAL ? 9'd34 : 9'd11);
	wire [ 9:0] c_dot = SENSE ? LIT_X : AIM_X;
	wire [18:0] k_line = PAL ? 19'd327073 : 19'd325222;
	reg  [27:0] c_sum;
	//28-bit sums; offset x 256, + 256 to round: PAL 144, NTSC 144 + 131 x (1271.1 - 1270.4) = 236.1
	always @(posedge CLK) c_sum <= c_line * k_line + c_dot * 9'd408 + (PAL ? 28'd37120 : 28'd60700);
	wire [19:0] counter = {c_sum[27:9], 1'b0};

	wire        seen = (SENSE ? LIT : AIM_ON) && !reload;
	wire [ 4:0] lines = SENSE ? LIT_N : 5'd24;
	always @(posedge CLK) begin
		DATA <= {8'h4D, trigger | reload, calib, 2'b00, calib, 2'b00, seen ? counter : 20'd0, seen ? lines : 5'd0};
	end

endmodule

//============================================================================
//  Video side of the light guns: crosshairs over the picture (gun 1 red, gun 2 green) and the
//  picture sensors.
//  SIZE: 0 small, 1 medium, 2 big (lines across the picture), 3 none. A gun aimed off the
//  picture has no crosshair.
//  Picture sensor (as the real gun): the field of a gun is a disc 25 lines across around its
//  aim (radius 12 pixels / lines); a place of the picture is lit when its brightness
//  (R + 2G + B) / 4 is at least LIT_MIN. For each field and each gun: the first lit place in
//  its field (in the order of the beam) and the number of lines with a lit place, handed to
//  clk_sys at the start of the vertical blank. The sensors look at RGB_IN, without the
//  crosshairs.
//============================================================================
module P3DO_GUN_VIDEO
#(
	parameter [7:0] LIT_MIN = 8'd128
)
(
	input             CLK,          // clk_sys, domain of the aims and of the sensor results
	input      [ 1:0] NUM,          // guns: 0, 1 or 2
	input      [ 1:0] SIZE,
	input      [ 9:0] AIM0_X,
	input      [ 7:0] AIM0_Y,
	input             AIM0_ON,
	input      [ 9:0] AIM1_X,
	input      [ 7:0] AIM1_Y,
	input             AIM1_ON,

	output reg        LIT0,         // the field of gun 1 had a lit place in the last field
	output reg [ 9:0] LIT0_X,       //   the first one: dot, line
	output reg [ 7:0] LIT0_Y,
	output reg [ 4:0] LIT0_N,       //   lines with a lit place
	output reg        LIT1,
	output reg [ 9:0] LIT1_X,
	output reg [ 7:0] LIT1_Y,
	output reg [ 4:0] LIT1_N,

	input             VCLK,         // video clock
	input             CE_PIX,
	input             HBL_N,
	input             VBL_N,
	input      [23:0] RGB_IN,
	output     [23:0] RGB_OUT       // RGB_IN where there is no crosshair
);

	// The aims, taken at the start of each vertical blank and handed to the video clock,
	// which copies them while they stay unchanged for a whole frame
	reg  [ 2:0] vbl_sync;
	reg  [ 8:0] s_x0, s_x1;
	reg  [ 7:0] s_y0, s_y1;
	reg         s_on0, s_on1, s_draw0, s_draw1;
	reg  [ 1:0] s_size;
	reg         s_tgl;
	always @(posedge CLK) begin
		vbl_sync <= {vbl_sync[1:0], VBL_N};
		if (vbl_sync[2] & ~vbl_sync[1]) begin
			s_x0 <= AIM0_X[9:1];
			s_y0 <= AIM0_Y;
			s_on0 <= AIM0_ON && NUM != 2'd0;
			s_draw0 <= SIZE != 2'd3;
			s_x1 <= AIM1_X[9:1];
			s_y1 <= AIM1_Y;
			s_on1 <= AIM1_ON && NUM[1];
			s_draw1 <= SIZE != 2'd3;
			s_size <= SIZE;
			s_tgl <= ~s_tgl;
		end
	end

	reg  [ 2:0] tgl_sync;
	reg  [ 8:0] x0, x1;
	reg  [ 7:0] y0, y1;
	reg         on0, on1, draw0, draw1;
	reg  [ 1:0] size;
	always @(posedge VCLK) begin
		tgl_sync <= {tgl_sync[1:0], s_tgl};
		if (tgl_sync[2] != tgl_sync[1]) begin
			x0 <= s_x0;
			y0 <= s_y0;
			on0 <= s_on0;
			draw0 <= s_draw0;
			x1 <= s_x1;
			y1 <= s_y1;
			on1 <= s_on1;
			draw1 <= s_draw1;
			size <= s_size;
		end
	end

	// Position of the pixel on RGB_IN (as the pause meter): dot of the active line, active line
	reg  [ 9:0] hx;
	reg  [ 8:0] vy;
	reg         hbl_old;
	always @(posedge VCLK) begin
		if (CE_PIX) begin
			hbl_old <= HBL_N;
			if (!HBL_N) hx <= '0;
			else if (~&hx) hx <= hx + 10'd1;
			if (!VBL_N) vy <= '0;
			else if (hbl_old && !HBL_N && ~&vy) vy <= vy + 9'd1;	// end of an active line
		end
	end
	wire [ 8:0] px = hx[9:1];	// 3DO pixel: 2 dots

	//------------------------------------------------------------------ crosshairs
	function automatic in_cross(input [8:0] fpx, input [8:0] fvy, input [8:0] cx, input [7:0] cy, input [1:0] fsize);
		reg [9:0] arm, x, y, cx1, cy1;
		begin
			arm = fsize == 2'd0 ? 10'd1 : 10'd3;
			x = {1'b0, fpx};
			y = {1'b0, fvy};
			cx1 = {1'b0, cx};
			cy1 = {2'b00, cy};
			in_cross = (y == cy1 && (fsize[1] || (x + arm >= cx1 && x <= cx1 + arm))) ||
			        (x == cx1 && (fsize[1] || (y + arm >= cy1 && y <= cy1 + arm)));
		end
	endfunction

	wire        c0 = on0 && draw0 && in_cross(px, vy, x0, y0, size);
	wire        c1 = on1 && draw1 && in_cross(px, vy, x1, y1, size);
	assign RGB_OUT = !(HBL_N && VBL_N) ? RGB_IN : c0 ? 24'hFF0000 : c1 ? 24'h00FF00 : RGB_IN;

	//------------------------------------------------------------------ picture sensors
	// inside the disc of radius 12 around the aim: dx^2 + dy^2 <= 12 x 13
	function automatic in_disc(input [8:0] fpx, input [8:0] fvy, input [8:0] cx, input [7:0] cy);
		reg [8:0] dx, dy;
		reg [8:0] d2;
		begin
			dx = fpx >= cx ? fpx - cx : cx - fpx;
			dy = fvy >= {1'b0, cy} ? fvy - {1'b0, cy} : {1'b0, cy} - fvy;
			d2 = dx[3:0] * dx[3:0] + dy[3:0] * dy[3:0];
			in_disc = dx <= 9'd12 && dy <= 9'd12 && d2 <= 9'd156;
		end
	endfunction

	wire [ 9:0] lum = {2'b00, RGB_IN[23:16]} + {1'b0, RGB_IN[15:8], 1'b0} + {2'b00, RGB_IN[7:0]};
	wire        lit = lum[9:2] >= LIT_MIN;
	wire        active = HBL_N && VBL_N;
	wire        see0 = on0 && active && lit && in_disc(px, vy, x0, y0);
	wire        see1 = on1 && active && lit && in_disc(px, vy, x1, y1);

	reg         f0, f1, l0, l1, vbl_old;
	reg  [ 9:0] fx0, fx1;
	reg  [ 7:0] fy0, fy1;
	reg  [ 4:0] n0, n1;
	reg         r_f0, r_f1, r_tgl;
	reg  [ 9:0] r_x0, r_x1;
	reg  [ 7:0] r_y0, r_y1;
	reg  [ 4:0] r_n0, r_n1;
	always @(posedge VCLK) begin
		if (CE_PIX) begin
			vbl_old <= VBL_N;
			if (see0) begin
				if (!f0) begin
					f0 <= 1'b1;
					fx0 <= hx;
					fy0 <= vy[7:0];
				end
				l0 <= 1'b1;
			end
			if (see1) begin
				if (!f1) begin
					f1 <= 1'b1;
					fx1 <= hx;
					fy1 <= vy[7:0];
				end
				l1 <= 1'b1;
			end
			if (hbl_old && !HBL_N) begin	// end of a line: count it if it was lit
				if (l0 && ~&n0) n0 <= n0 + 5'd1;
				if (l1 && ~&n1) n1 <= n1 + 5'd1;
				l0 <= 1'b0;
				l1 <= 1'b0;
			end
			if (vbl_old && !VBL_N) begin	// start of the vertical blank: results of the field
				r_f0 <= f0;
				r_x0 <= fx0;
				r_y0 <= fy0;
				r_n0 <= n0;
				r_f1 <= f1;
				r_x1 <= fx1;
				r_y1 <= fy1;
				r_n1 <= n1;
				r_tgl <= ~r_tgl;
				f0 <= 1'b0;
				f1 <= 1'b0;
				l0 <= 1'b0;
				l1 <= 1'b0;
				n0 <= 5'd0;
				n1 <= 5'd0;
			end
		end
	end

	// results to clk_sys: stable for a whole field after the toggle
	reg  [ 2:0] r_sync;
	always @(posedge CLK) begin
		r_sync <= {r_sync[1:0], r_tgl};
		if (r_sync[2] != r_sync[1]) begin
			LIT0 <= r_f0;
			LIT0_X <= r_x0;
			LIT0_Y <= r_y0;
			LIT0_N <= r_n0;
			LIT1 <= r_f1;
			LIT1_X <= r_x1;
			LIT1_Y <= r_y1;
			LIT1_N <= r_n1;
		end
	end

endmodule

//============================================================================
//  PlayStation GunCon (Namco) on the SNAC port, read as the light guns of the 3DO.
//
//  The GunCon times the beam itself: it takes the composite sync of the core (pin 6 of the
//  SNAC adapter, as the MiSTer PlayStation core sends it to a GunCon) and the light of the
//  CRT, and answers the pad protocol of the PlayStation with its buttons and the place it
//  saw: X in 8 MHz clocks from the horizontal sync, Y in lines from the vertical sync
//  (X = 1 and Y = 10 when it saw nothing). Both ports (ATT 1, ATT 2) are read once per
//  field, from the start of the vertical blank. Pins as the PlayStation core and its SNAC
//  adapter: 0 ATT port 2, 1 ATT port 1, 2 CMD, 3 ACK (in), 4 DAT (in), 5 CLK, 6 sync out.
//  Transfer: 250 kHz clock, data LSB first, CMD changed on the falling edge of CLK, DAT
//  read on the rising edge; the device pulls ACK low after each byte but the last.
//  Request 01 42 00 00 00 00 00 00 00; GunCon answer FF 63 5A, buttons (2 bytes, active low:
//  bit 13 trigger, bit 3 A, bit 14 B), X (2 bytes), Y (2 bytes).
//
//  The aim given to the 3DO gun: dot = (X - XOFS) x 12.27 / 8 + XDOT + 4 x XADJ, line = Y - YOFS,
//  from the sync of the core: the horizontal sync starts 121 dots (79 GunCon clocks) before dot 0
//  of the next line, the vertical sync starts 22 lines before the first picture line. On a real
//  GunCon (2026-11-14, Namco GunCon on a JVC CRT) the crosshair sat 18 pixels left of the aim with
//  that alone; 18 pixels more (XDOT = 36 dots, about 2.9 us: the GunCon counts from a little later
//  than the start of the sync) put it on the aim at the centre and at both edges, and the line was
//  right. XADJ (menu: GunCon X adjust) moves the aim from there, and with it the crosshair and the
//  3DO gun counter, by 2 pixels (4 dots) a step, -32 to +30 pixels.
//============================================================================
module P3DO_GUNCON
#(
	parameter [15:0] XOFS = 16'd79,
	parameter [15:0] YOFS = 16'd22,
	parameter  [7:0] XDOT = 8'd36
)
(
	input             CLK,          // 50 MHz
	input             RST,
	input             ENABLE,
	input             VBL_N,        // any clock domain: the reading starts with the vertical blank
	input       [4:0] XADJ,         // horizontal adjustment, signed: XADJ x 2 pixels (-16..15 -> -32..+30)

	input       [6:0] USER_IN,
	output      [5:0] USER_OUT,     // pins 0-5 (pin 6, the sync, is sent by the top level)

	output reg  [1:0] PRESENT,      // a GunCon answered on port 1 / port 2
	output reg  [9:0] AIM0_X,       // port 1: dot of the active line
	output reg  [7:0] AIM0_Y,       //         active line
	output reg        AIM0_ON,      //         on the picture
	output reg  [2:0] BTN0,         //         {B, A, trigger}
	output reg  [9:0] AIM1_X,       // port 2
	output reg  [7:0] AIM1_Y,
	output reg        AIM1_ON,
	output reg  [2:0] BTN1
);

	localparam HALF = 13'd100;		// 2 us: 250 kHz clock
	localparam ATT_WAIT = 13'd1000;	// 20 us from ATT to the first byte
	localparam ACK_WAIT = 13'd5000;	// 100 us for the ACK of a byte
	localparam GAP = 13'd500;		// 10 us after the ACK
	localparam PORT_GAP = 13'd5000;	// 100 us between the two ports

	reg  [2:0] ack_s, dat_s, vbl_s;
	always @(posedge CLK) begin
		ack_s <= {ack_s[1:0], USER_IN[3]};
		dat_s <= {dat_s[1:0], USER_IN[4]};
		vbl_s <= {vbl_s[1:0], VBL_N};
	end
	wire start = vbl_s[2] & ~vbl_s[1];

	typedef enum bit [2:0] {S_IDLE, S_ATT, S_LOW, S_HIGH, S_ACK, S_GAP, S_END, S_PORT} state_t;
	state_t st;
	reg        port, att, clk_o, cmd, acked;
	reg  [3:0] byte_i;
	reg  [2:0] bit_i;
	reg  [7:0] rx;
	reg  [7:0] rxb[9];
	reg [12:0] cnt;

	wire [7:0] tx = byte_i == 4'd0 ? 8'h01 : byte_i == 4'd1 ? 8'h42 : 8'h00;

	// answer of the GunCon: buttons, X, Y
	wire [15:0] btn = ~{rxb[4], rxb[3]};
	wire [15:0] gx = {rxb[6], rxb[5]};
	wire [15:0] gy = {rxb[8], rxb[7]};
	wire        is_guncon = rxb[1] == 8'h63 && rxb[2] == 8'h5A;
	wire        light = !(gx == 16'd1 && gy == 16'd10) && gy >= YOFS;
	wire signed [17:0] gx0 = $signed({2'b00, gx}) - $signed({2'b00, XOFS});
	wire signed [29:0] dotm = gx0 * $signed(30'd1571);	// x 12.2727 / 8 = 1571 / 1024
	wire signed [19:0] dot = $signed(dotm[29:10]) + $signed({12'd0, XDOT}) + $signed({{13{XADJ[4]}}, XADJ, 2'b00});	// + XDOT + XADJ x 2 pixels (4 dots)
	wire [15:0] line = gy - YOFS;
	wire        on = light && !dot[19] && dot < $signed(20'd640) && line < 16'd240;

	always @(posedge CLK) begin
		if (RST || !ENABLE) begin
			st <= S_IDLE;
			att <= 1'b0;
			clk_o <= 1'b1;
			cmd <= 1'b1;
			PRESENT <= 2'b00;
			AIM0_ON <= 1'b0;
			AIM1_ON <= 1'b0;
			BTN0 <= 3'b000;
			BTN1 <= 3'b000;
		end
		else begin
			if (cnt != 13'd0) cnt <= cnt - 13'd1;
			case (st)
				S_IDLE:
					if (start) begin
						port <= 1'b0;
						att <= 1'b1;
						cnt <= ATT_WAIT;
						st <= S_ATT;
					end

				S_ATT:
					if (cnt == 13'd0) begin
						byte_i <= 4'd0;
						bit_i <= 3'd0;
						st <= S_LOW;
						cnt <= HALF;
						clk_o <= 1'b0;
						cmd <= 1'b1;	// tx[0] of 01 is set just below (S_LOW sets each bit)
					end

				S_LOW: begin
					clk_o <= 1'b0;
					cmd <= tx[bit_i];
					if (cnt == 13'd0) begin
						clk_o <= 1'b1;
						rx <= {dat_s[2], rx[7:1]};	// read on the rising edge, LSB first
						cnt <= HALF;
						st <= S_HIGH;
					end
				end

				S_HIGH:
					if (cnt == 13'd0) begin
						if (bit_i != 3'd7) begin
							bit_i <= bit_i + 3'd1;
							clk_o <= 1'b0;
							cnt <= HALF;
							st <= S_LOW;
						end
						else begin
							rxb[byte_i] <= rx;
							bit_i <= 3'd0;
							cmd <= 1'b1;
							if (byte_i == 4'd8) st <= S_END;
							else begin
								acked <= 1'b0;
								cnt <= ACK_WAIT;
								st <= S_ACK;
							end
						end
					end

				S_ACK: begin
					if (!ack_s[2]) acked <= 1'b1;
					if (acked && ack_s[2]) begin	// ACK pulse over
						cnt <= GAP;
						st <= S_GAP;
					end
					else if (cnt == 13'd0) begin			// no device, or it stopped answering
						rxb[1] <= 8'h00;
						st <= S_END;
					end
				end

				S_GAP:
					if (cnt == 13'd0) begin
						byte_i <= byte_i + 4'd1;
						clk_o <= 1'b0;
						cnt <= HALF;
						st <= S_LOW;
					end

				S_END: begin
					att <= 1'b0;
					PRESENT[port] <= is_guncon;
					if (!port) begin
						AIM0_X <= dot[9:0];
						AIM0_Y <= line[7:0];
						AIM0_ON <= is_guncon && on;
						BTN0 <= is_guncon ? {btn[14], btn[3], btn[13]} : 3'b000;
						port <= 1'b1;
						cnt <= PORT_GAP;
						st <= S_PORT;
					end
					else begin
						AIM1_X <= dot[9:0];
						AIM1_Y <= line[7:0];
						AIM1_ON <= is_guncon && on;
						BTN1 <= is_guncon ? {btn[14], btn[3], btn[13]} : 3'b000;
						st <= S_IDLE;
					end
				end

				S_PORT:
					if (cnt == 13'd0) begin
						att <= 1'b1;
						cnt <= ATT_WAIT;
						st <= S_ATT;
					end

				default: st <= S_IDLE;
			endcase
		end
	end

	assign USER_OUT = ENABLE ? {clk_o, 1'b1, 1'b1, cmd, ~(att & ~port), ~(att & port)} : 6'b000000;

endmodule
