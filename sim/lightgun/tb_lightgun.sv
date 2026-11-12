`timescale 1ns/1ps
//============================================================================
//  Light gun (rtl/lightgun.sv) testbench.
//
//  Record: for aims given with the joystick (the 9 crosses of the bench page PISTOLET CIBLES,
//  the centre, the corners), the record must be 4D, the buttons, the counter and 24 lines,
//  the counter equal to the integer formula and within 2 of 1270.4 x (line + 11) + 236.1 +
//  1.595 x dot; at the extreme values of the stick and with the reload button, counter
//  and line count 0. Buttons: trigger 80, calibration 48, both C8, reload 80.
//  Mouse: the aim starts at the centre, moves 2 dots / 1 line per count, leaves the
//  picture (nothing seen), stops at the margin and comes back; left, right, middle buttons.
//  Crosshairs: one frame of a 640x240 picture (780x262 with blanking) for each size, with
//  gun 1 at pixel 100 line 50 and gun 2 at pixel 200 line 100: the red and green dots must
//  be exactly the crosses, the others unchanged; none when the size is "none", for a gun
//  off the picture or not in use. Writes the small-cross frame to cross.ppm.
//  Prints "TB PASS" or "TB FAIL".
//============================================================================
module tb_lightgun;

	reg         clk = 0, vclk = 0;
	always #10 clk = ~clk;			// clk_sys 50 MHz
	always #10.185 vclk = ~vclk;	// 49.09 MHz, one dot per clock here

	integer     errors = 0;

	//------------------------------------------------------------------ record
	reg         rst = 1;
	reg         pal = 0;
	reg         mouse_mode = 0;
	reg  [ 7:0] joy_x = 0, joy_y = 0;
	reg         j_trig = 0, j_reload = 0, j_calib = 0;
	reg  [24:0] ps2 = 0;
	wire [39:0] data;
	wire [ 9:0] aim_x;
	wire [ 7:0] aim_y;
	wire        aim_on;

	P3DO_LIGHTGUN gun
	(
		.CLK(clk), .RST(rst), .PAL(pal), .MOUSE_MODE(mouse_mode), .EXT_MODE(1'b0),
		.JOY_X(joy_x), .JOY_Y(joy_y), .JOY_TRIGGER(j_trig), .JOY_RELOAD(j_reload), .JOY_CALIB(j_calib),
		.PS2_MOUSE(ps2),
		.EXT_X(10'd0), .EXT_Y(8'd0), .EXT_ON(1'b0), .EXT_TRIGGER(1'b0), .EXT_RELOAD(1'b0), .EXT_CALIB(1'b0),
		.SENSE(1'b0), .LIT(1'b0), .LIT_X(10'd0), .LIT_Y(8'd0), .LIT_N(5'd0),
		.DATA(data), .AIM_X(aim_x), .AIM_Y(aim_y), .AIM_ON(aim_on)
	);

	//------------------------------------------------------------------ GunCon on the SNAC port
	reg         gc_en = 0;
	reg         gc_vbl_n = 1;
	wire [ 5:0] uo;
	reg         dat_o = 1, ack_o = 1;
	wire [ 6:0] ui = {1'b1, 1'b1, dat_o, ack_o, 3'b111};
	wire [ 1:0] gc_present;
	wire [ 9:0] gc_x0, gc_x1;
	wire [ 7:0] gc_y0, gc_y1;
	wire        gc_on0, gc_on1;
	wire [ 2:0] gc_b0, gc_b1;

	P3DO_GUNCON guncon
	(
		.CLK(clk), .RST(rst), .ENABLE(gc_en), .VBL_N(gc_vbl_n),
		.USER_IN(ui), .USER_OUT(uo),
		.PRESENT(gc_present),
		.AIM0_X(gc_x0), .AIM0_Y(gc_y0), .AIM0_ON(gc_on0), .BTN0(gc_b0),
		.AIM1_X(gc_x1), .AIM1_Y(gc_y1), .AIM1_ON(gc_on1), .BTN1(gc_b1)
	);

	// 3DO gun aimed by the GunCon of port 1: trigger, B reload, A calibration button
	wire [39:0] ext_data;
	wire [ 9:0] ext_x;
	wire [ 7:0] ext_y;
	wire        ext_on;
	P3DO_LIGHTGUN gun_ext
	(
		.CLK(clk), .RST(rst), .PAL(1'b0), .MOUSE_MODE(1'b0), .EXT_MODE(1'b1),
		.JOY_X(8'd0), .JOY_Y(8'd0), .JOY_TRIGGER(1'b0), .JOY_RELOAD(1'b0), .JOY_CALIB(1'b0),
		.PS2_MOUSE(25'd0),
		.EXT_X(gc_x0), .EXT_Y(gc_y0), .EXT_ON(gc_on0), .EXT_TRIGGER(gc_b0[0]), .EXT_RELOAD(gc_b0[2]), .EXT_CALIB(gc_b0[1]),
		.SENSE(1'b0), .LIT(1'b0), .LIT_X(10'd0), .LIT_Y(8'd0), .LIT_N(5'd0),
		.DATA(ext_data), .AIM_X(ext_x), .AIM_Y(ext_y), .AIM_ON(ext_on)
	);

	// GunCon model: answer of each port (buttons pressed = 1 here), plugged or not
	reg  [15:0] m_btn[2], m_x[2], m_y[2];
	reg         m_plug[2];
	integer     m_byte = 0, m_bit = 0, m_port = -1;
	wire        att1_n = uo[1], att2_n = uo[0], pclk = uo[5], pcmd = uo[2];
	reg  [ 7:0] m_cmd, m_r;
	reg  [ 7:0] m_cmds[2][9];

	function automatic [7:0] m_reply(input integer p, input integer b);
		case (b)
			0: m_reply = 8'hFF;
			1: m_reply = 8'h63;
			2: m_reply = 8'h5A;
			3: m_reply = ~m_btn[p][7:0];
			4: m_reply = ~m_btn[p][15:8];
			5: m_reply = m_x[p][7:0];
			6: m_reply = m_x[p][15:8];
			7: m_reply = m_y[p][7:0];
			default: m_reply = m_y[p][15:8];
		endcase
	endfunction

	always @(negedge att1_n or negedge att2_n) begin
		if (gc_en) begin
			m_port = !att1_n ? 0 : 1;
			m_byte = 0;
			m_bit = 0;
		end
	end
	always @(posedge att1_n or posedge att2_n) begin
		if (att1_n && att2_n) begin
			m_port = -1;
			dat_o = 1;
		end
	end
	always @(negedge pclk) begin
		if (gc_en && m_port >= 0 && m_plug[m_port] && m_byte < 9) begin
			m_r = m_reply(m_port, m_byte);
			dat_o = m_r[m_bit];
		end
	end
	always @(posedge pclk) begin
		if (gc_en && m_port >= 0 && m_plug[m_port]) begin
			m_cmd[m_bit] = pcmd;
			if (m_bit == 7) begin
				m_cmds[m_port][m_byte] = m_cmd;
				m_bit = 0;
				if (m_byte < 8) fork begin #8000; ack_o = 0; #3000; ack_o = 1; end join_none
				m_byte++;
			end
			else m_bit++;
		end
	end

	task automatic gc_read;
		begin
			gc_vbl_n = 0;
			#20000;
			gc_vbl_n = 1;
			#2000000;	// both ports are read in about 1.2 ms
		end
	endtask

	task automatic gc_check(input bit [1:0] present, input bit on0, input integer x0, input integer y0, input bit [2:0] b0,
	                        input bit on1, input bit [2:0] b1, input string what);
		begin
			if (gc_present != present || gc_on0 != on0 || (on0 && (gc_x0 != 10'(x0) || gc_y0 != 8'(y0))) || gc_b0 != b0 ||
			    gc_on1 != on1 || gc_b1 != b1) begin
				$display("TB error GunCon %s: present %b, port 1 on %0d at %0d,%0d buttons %b, port 2 on %0d buttons %b",
				         what, gc_present, gc_on0, gc_x0, gc_y0, gc_b0, gc_on1, gc_b1);
				errors++;
			end
			else $display("TB GunCon %-30s present %b, port 1 on %0d dot %3d line %3d buttons %b, port 2 on %0d buttons %b",
			              what, gc_present, gc_on0, gc_x0, gc_y0, gc_b0, gc_on1, gc_b1);
		end
	endtask

	function automatic [19:0] rec_counter(input [39:0] d);
		rec_counter = {d[24], d[23:16], d[15:8], d[7:5]};
	endfunction

	task automatic settle;
		repeat (8) @(posedge clk);
	endtask

	// expected record for an aim on the picture
	task automatic check_on(input integer dot, input integer line, input [7:0] buttons, input string what);
		longint sum;
		integer exp_c, c;
		real model;
		begin
			sum = longint'(line + (pal ? 34 : 11)) * (pal ? 327073 : 325222) + longint'(dot) * 408 + (pal ? 37120 : 60700);
			exp_c = int'(sum >> 9) * 2;
			model = (pal ? 1277.63 : 1270.4) * (line + (pal ? 34 : 11)) + (pal ? 144.0 : 236.1) + 1.595 * dot;
			c = int'(rec_counter(data));
			if (data[39:32] != 8'h4D || {data[31:25], 1'b0} != buttons || c != exp_c || data[4:0] != 5'd24 ||
			    aim_x != 10'(dot) || aim_y != 8'(line) || !aim_on || c[0] ||
			    (real'(c) - model) > 2.0 || (model - real'(c)) > 2.0) begin	// even rounding (1) + 408/256 for 1.595 (0.8 at dot 639)
				$display("TB error %s: record %010X (counter %0d lines %0d) aim %0d,%0d on %0d; expected dot %0d line %0d counter %0d (model %.1f) buttons %02X",
				         what, data, c, data[4:0], aim_x, aim_y, aim_on, dot, line, exp_c, model, buttons);
				errors++;
			end
			else $display("TB %-24s dot %3d line %3d -> %02X %02X %02X %02X %02X  counter %6d (model %8.1f) lines %0d",
			              what, dot, line, data[39:32], data[31:24], data[23:16], data[15:8], data[7:0], c, model, data[4:0]);
		end
	endtask

	task automatic check_off(input [7:0] buttons, input string what);
		begin
			if (data[39:32] != 8'h4D || data[31:24] != buttons || data[23:0] != 24'd0 || aim_on != (what.substr(0, 5) == "reload")) begin
				$display("TB error %s: record %010X aim on %0d, expected 4D %02X 00 00 00", what, data, aim_on, buttons);
				errors++;
			end
			else $display("TB %-24s nothing seen   -> %02X %02X %02X %02X %02X", what, data[39:32], data[31:24], data[23:16], data[15:8], data[7:0]);
		end
	endtask

	function automatic integer jdot(input integer j);
		jdot = ((j + 128) * 5) / 2;
	endfunction
	function automatic integer jline(input integer j);
		jline = ((j + 128) * 15) / 16;
	endfunction

	// one PS/2 packet: dx, dy (up is positive), buttons {middle, right, left}, x overflow
	task automatic mouse(input integer dx, input integer dy, input [2:0] btn, input bit xovf = 0);
		reg [8:0] x9, y9;
		begin
			x9 = 9'(dx);
			y9 = 9'(dy);
			ps2 = {~ps2[24], y9[7:0], x9[7:0], 1'b0, xovf, y9[8], x9[8], 1'b1, btn[2], btn[1], btn[0]};
			settle();
		end
	endtask

	//------------------------------------------------------------------ crosshairs
	localparam int H_ACT = 640, H_TOT = 780, V_ACT = 240, V_TOT = 262;
	reg  [ 1:0] num = 2, size = 0;
	reg  [ 9:0] a0x = 200, a1x = 401;
	reg  [ 7:0] a0y = 50, a1y = 100;
	reg         a0on = 1, a1on = 1;
	integer     hx = 0, vy = 0;
	wire        hbl_n = hx < H_ACT;
	wire        vbl_n = vy < V_ACT;
	// picture: grey (crosshair tests), or black with a white block at pixels 150-169, lines 100-139 (sensor tests)
	reg         pic_mode = 0;
	wire [23:0] rgb_in = !pic_mode ? 24'h808080 : (hx >= 300 && hx < 340 && vy >= 100 && vy < 140) ? 24'hFFFFFF : 24'h000000;
	wire [23:0] rgb_out;
	always @(posedge vclk) begin
		if (hx == H_TOT - 1) begin
			hx <= 0;
			vy <= vy == V_TOT - 1 ? 0 : vy + 1;
		end
		else hx <= hx + 1;
	end

	wire        lit0, lit1;
	wire [ 9:0] lit0_x, lit1_x;
	wire [ 7:0] lit0_y, lit1_y;
	wire [ 4:0] lit0_n, lit1_n;
	P3DO_GUN_VIDEO crosshairs
	(
		.CLK(clk), .NUM(num), .SIZE(size),
		.AIM0_X(a0x), .AIM0_Y(a0y), .AIM0_ON(a0on),
		.AIM1_X(a1x), .AIM1_Y(a1y), .AIM1_ON(a1on),
		.LIT0(lit0), .LIT0_X(lit0_x), .LIT0_Y(lit0_y), .LIT0_N(lit0_n),
		.LIT1(lit1), .LIT1_X(lit1_x), .LIT1_Y(lit1_y), .LIT1_N(lit1_n),
		.VCLK(vclk), .CE_PIX(1'b1), .HBL_N(hbl_n), .VBL_N(vbl_n),
		.RGB_IN(rgb_in), .RGB_OUT(rgb_out)
	);

	// 3DO gun with the picture sensor of gun 1
	wire [39:0] sense_data;
	P3DO_LIGHTGUN gun_sense
	(
		.CLK(clk), .RST(rst), .PAL(1'b0), .MOUSE_MODE(1'b0), .EXT_MODE(1'b0),
		.JOY_X(8'd0), .JOY_Y(8'd0), .JOY_TRIGGER(1'b1), .JOY_RELOAD(1'b0), .JOY_CALIB(1'b0),
		.PS2_MOUSE(25'd0),
		.EXT_X(10'd0), .EXT_Y(8'd0), .EXT_ON(1'b0), .EXT_TRIGGER(1'b0), .EXT_RELOAD(1'b0), .EXT_CALIB(1'b0),
		.SENSE(1'b1), .LIT(lit0), .LIT_X(lit0_x), .LIT_Y(lit0_y), .LIT_N(lit0_n),
		.DATA(sense_data), .AIM_X(), .AIM_Y(), .AIM_ON()
	);

	task automatic check_sense(input bit lit, input integer x, input integer y, input integer n, input string what);
		longint sum;
		integer c, rc;
		begin
			repeat (3) begin
				do @(negedge vclk); while (!(hx == 0 && vy == 0));
			end
			repeat (20) @(posedge clk);
			rc = int'(rec_counter(sense_data));
			sum = longint'(y + 23) * 325222 + longint'(x) * 408 + 60700;
			c = lit ? int'(sum >> 9) * 2 : 0;
			if (lit0 != lit || (lit && (lit0_x != 10'(x) || lit0_y != 8'(y) || lit0_n != 5'(n))) || rc != c ||
			    sense_data[4:0] != (lit ? 5'(n) : 5'd0) || sense_data[39:32] != 8'h4D || sense_data[31] != 1'b1) begin
				$display("TB error sensor %s: lit %0d first dot %0d line %0d, %0d lines, record %010X; expected %0d %0d %0d %0d counter %0d",
				         what, lit0, lit0_x, lit0_y, lit0_n, sense_data, lit, x, y, n, c);
				errors++;
			end
			else $display("TB sensor %-26s lit %0d first dot %3d line %3d, %2d lines -> %02X %02X %02X %02X %02X (counter %0d)",
			              what, lit0, lit0_x, lit0_y, lit0_n, sense_data[39:32], sense_data[31:24], sense_data[23:16],
			              sense_data[15:8], sense_data[7:0], rc);
		end
	endtask

	reg  [23:0] frame[V_ACT][H_ACT];
	task automatic grab;
		begin
			// two frames for the aims to reach the video side, then one frame captured
			repeat (3) begin
				do @(negedge vclk); while (!(hx == 0 && vy == 0));
			end
			frame[0][0] = rgb_out;
			do begin
				@(negedge vclk);
				if (hx < H_ACT && vy < V_ACT) frame[vy][hx] = rgb_out;
			end while (!(hx == H_TOT - 1 && vy == V_TOT - 1));
		end
	endtask

	function automatic bit on_cross(input integer px, input integer l, input integer cx, input integer cy, input integer sz);
		integer arm;
		begin
			arm = sz == 0 ? 1 : 3;
			on_cross = (l == cy && (sz == 2 || (px >= cx - arm && px <= cx + arm))) ||
			           (px == cx && (sz == 2 || (l >= cy - arm && l <= cy + arm)));
		end
	endfunction

	task automatic check_frame(input integer sz, input bit g0, input bit g1, input string what);
		integer bad, reds, greens;
		reg [23:0] want;
		begin
			bad = 0; reds = 0; greens = 0;
			for (int l = 0; l < V_ACT; l++)
				for (int d = 0; d < H_ACT; d++) begin
					want = 24'h808080;
					if (g1 && sz != 3 && on_cross(d / 2, l, a1x / 2, a1y, sz)) want = 24'h00FF00;
					if (g0 && sz != 3 && on_cross(d / 2, l, a0x / 2, a0y, sz)) want = 24'hFF0000;
					if (frame[l][d] == 24'hFF0000) reds++;
					if (frame[l][d] == 24'h00FF00) greens++;
					if (frame[l][d] != want) begin
						if (bad < 5) $display("TB error %s: line %0d dot %0d is %06X, expected %06X", what, l, d, frame[l][d], want);
						bad++;
					end
				end
			if (bad) errors++;
			$display("TB crosshair %-22s %5d red dots, %5d green dots, %0d wrong", what, reds, greens, bad);
		end
	endtask

	integer f;
	initial begin
		// joystick: centre, the 9 crosses of the bench (pixels 40/160/280, lines 40/120/200), corners, edges
		repeat (4) @(posedge clk);
		rst = 0;
		settle();
		check_on(320, 120, 8'h00, "centre");
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++) begin
				joy_x = 8'(c == 0 ? -96 : c == 1 ? 0 : 96);
				joy_y = 8'(r == 0 ? -85 : r == 1 ? 0 : 86);
				j_trig = 1;
				settle();
				check_on(jdot($signed(joy_x)), jline($signed(joy_y)), 8'h80, $sformatf("cross T%0d (trigger)", r * 3 + c + 1));
			end
		j_trig = 0;
		joy_x = 8'(-126); joy_y = 8'(-126); settle(); check_on(5, 1, 8'h00, "top left corner");
		joy_x = 8'd126;   joy_y = 8'd126;   settle(); check_on(635, 238, 8'h00, "bottom right corner");
		joy_x = 8'h80;    joy_y = 8'd0;     settle(); check_off(8'h00, "stick x at -128");
		joy_x = 8'd127;   joy_y = 8'd0;     settle(); check_off(8'h00, "stick x at 127");
		joy_x = 8'd0;     joy_y = 8'(-127); settle(); check_off(8'h00, "stick y at -127");
		joy_x = 8'd0;     joy_y = 8'd0;
		j_calib = 1;               settle(); check_on(320, 120, 8'h48, "calibration button");
		j_trig = 1;                settle(); check_on(320, 120, 8'hC8, "trigger + calibration");
		j_calib = 0; j_reload = 1; settle(); check_off(8'h80, "reload, trigger held");
		j_trig = 0;                settle(); check_off(8'h80, "reload");
		j_reload = 0;
		pal = 1;                   settle(); check_on(320, 120, 8'h00, "centre, PAL");
		pal = 0;

		// mouse
		mouse_mode = 1;
		settle();                 check_on(320, 120, 8'h00, "mouse at the start");
		mouse(10, 5, 3'b000);     check_on(340, 115, 8'h00, "mouse +10 +5 (up)");
		mouse(0, 0, 3'b001);      check_on(340, 115, 8'h80, "mouse left button");
		mouse(0, 0, 3'b100);      check_on(340, 115, 8'h48, "mouse middle button");
		mouse(0, 0, 3'b010);      check_off(8'h80, "reload: right button");
		mouse(-255, 0, 3'b000);   check_off(8'h00, "mouse -255: off");
		mouse(-255, 0, 3'b000);   check_off(8'h00, "mouse -255 again: margin");
		mouse(200, 0, 3'b000);    check_on(336, 115, 8'h00, "mouse back from -64");
		mouse(0, 127, 3'b000);    check_off(8'h00, "mouse above the picture");
		mouse(0, -127, 3'b000);   check_on(336, 115, 8'h00, "mouse back down");
		mouse(0, 0, 3'b000, 1);   check_off(8'h00, "x overflow: right margin");
		mouse(-200, 0, 3'b000);   check_on(303, 115, 8'h00, "mouse back from 703");
		mouse_mode = 0;
		settle();                 check_on(320, 120, 8'h00, "joystick again");

		// crosshairs
		size = 0; grab(); check_frame(0, 1, 1, "small");
		f = $fopen("cross.ppm", "w");
		$fwrite(f, "P3\n%0d %0d\n255\n", H_ACT, V_ACT);
		for (int l = 0; l < V_ACT; l++) begin
			for (int d = 0; d < H_ACT; d++) $fwrite(f, "%0d %0d %0d ", frame[l][d][23:16], frame[l][d][15:8], frame[l][d][7:0]);
			$fwrite(f, "\n");
		end
		$fclose(f);
		size = 1; grab(); check_frame(1, 1, 1, "medium");
		size = 2; grab(); check_frame(2, 1, 1, "big");
		size = 3; grab(); check_frame(3, 1, 1, "none");
		size = 0; a0on = 0; grab(); check_frame(0, 0, 1, "gun 1 off the picture");
		a0on = 1; num = 1; grab(); check_frame(0, 1, 0, "one gun");
		num = 0;  grab(); check_frame(0, 0, 0, "no gun");

		// picture sensor of gun 1 (disc of radius 12 around the aim): black picture with a white block, pixels 150-169, lines 100-139
		pic_mode = 1; num = 1; size = 3; a0on = 1;
		a0x = 320; a0y = 120; check_sense(1, 314, 108, 25, "aim 160,120: whole disc lit");
		a0x = 320; a0y = 95;  check_sense(1, 300, 100, 8, "aim 160,95: lit from 100");
		a0x = 320; a0y = 60;  check_sense(0, 0, 0, 0, "aim 160,60: dark");
		a0x = 360; a0y = 120; check_sense(1, 338, 115, 11, "aim 180,120: left side lit");
		a0x = 320; a0y = 120; a0on = 0; check_sense(0, 0, 0, 0, "aim off the picture");
		a0on = 1; pic_mode = 0; num = 2;

		// GunCon: port 1 seen at X = 79 + 245 (dot 375), Y = 22 + 100 (line 100) with the trigger; port 2 unplugged
		m_plug[0] = 1; m_plug[1] = 0;
		m_btn[0] = 16'h2000; m_x[0] = 16'd324; m_y[0] = 16'd122;
		m_btn[1] = 16'h0000; m_x[1] = 16'd1; m_y[1] = 16'd10;
		gc_en = 1;
		#100000;
		gc_read();
		gc_check(2'b01, 1, 375, 100, 3'b001, 0, 3'b000, "trigger, port 2 unplugged");
		for (int b = 0; b < 9; b++)
			if (m_cmds[0][b] != (b == 0 ? 8'h01 : b == 1 ? 8'h42 : 8'h00)) begin
				$display("TB error GunCon: command byte %0d is %02X", b, m_cmds[0][b]);
				errors++;
			end
		begin
			longint sum;
			integer c;
			sum = longint'(100 + 11) * 325222 + longint'(375) * 408 + 60700;
			c = int'(sum >> 9) * 2;
			if (ext_data != {8'h4D, 8'h80 | 8'(c >> 19), 8'(c >> 11), 8'(c >> 3), 3'(c), 5'd24}) begin
				$display("TB error GunCon gun record %010X, expected counter %0d", ext_data, c);
				errors++;
			end
			else $display("TB GunCon 3DO gun record %02X %02X %02X %02X %02X (counter %0d, 24 lines)",
			              ext_data[39:32], ext_data[31:24], ext_data[23:16], ext_data[15:8], ext_data[7:0], c);
		end
		// port 1: B (reload); port 2 plugged, seeing nothing (X 1, Y 10), button A
		m_btn[0] = 16'h4000; m_plug[1] = 1; m_btn[1] = 16'h0008;
		gc_read();
		gc_check(2'b11, 1, 375, 100, 3'b100, 0, 3'b010, "B on 1, nothing seen + A on 2");
		if (ext_data != 40'h4D80000000) begin
			$display("TB error GunCon reload record %010X", ext_data);
			errors++;
		end
		else $display("TB GunCon 3DO gun record with B (reload): %010X", ext_data);
		// edges of the picture: X 79 (dot 0), 496 (dot 639), 497 (off), Y 22 + 239 (line 239), 22 + 240 (off)
		m_btn[0] = 16'h0000; m_x[0] = 16'd79; m_y[0] = 16'd261;
		gc_read(); gc_check(2'b11, 1, 0, 239, 3'b000, 0, 3'b010, "dot 0, line 239");
		m_x[0] = 16'd496; m_y[0] = 16'd22;
		gc_read(); gc_check(2'b11, 1, 639, 0, 3'b000, 0, 3'b010, "dot 639, line 0");
		m_x[0] = 16'd497;
		gc_read(); gc_check(2'b11, 0, 0, 0, 3'b000, 0, 3'b010, "dot 640: off");
		m_x[0] = 16'd200; m_y[0] = 16'd262;
		gc_read(); gc_check(2'b11, 0, 0, 0, 3'b000, 0, 3'b010, "line 240: off");
		// both unplugged
		m_plug[0] = 0; m_plug[1] = 0;
		gc_read(); gc_check(2'b00, 0, 0, 0, 3'b000, 0, 3'b000, "unplugged");

		if (errors == 0) $display("TB PASS");
		else $display("TB FAIL (%0d errors)", errors);
		$finish;
	end

endmodule
