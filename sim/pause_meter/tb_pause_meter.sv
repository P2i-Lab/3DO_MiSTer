`timescale 1ns/1ps
//============================================================================
//  Pause meter (rtl/pause_meter.sv) testbench.
//
//  A 288x64 picture (300x72 with blanking) and a pause pattern repeated every
//  100 clocks: paused 25 % of the time, with the DDR3 VRAM busy 10 %, a VRAM
//  page transfer running 10 % and the ROM/NVRAM busy 5 %. With the meter on,
//  the bars must be 64, 25, 25 and 12 pixels long (256 * share, rounded down,
//  one pixel of tolerance), the box drawn at its place and the rest of the
//  picture unchanged; with the meter off the picture must be unchanged.
//  Writes the picture with the meter to meter.ppm. Prints "TB PASS" or "TB FAIL".
//============================================================================
module tb_pause_meter;

	localparam int H_ACT = 288, H_TOT = 300, V_ACT = 64, V_TOT = 72;

	reg         clk = 0, vclk = 0;
	always #10 clk = ~clk;		// clk_sys 50 MHz
	always #20 vclk = ~vclk;	// one pixel per video clock

	reg         enable = 0;
	integer     c = 0;
	wire [ 6:0] phase = 7'(c % 100);
	wire        pause = phase < 25;
	wire        ddr_busy = phase < 10;
	wire        vram_wait = phase >= 10 && phase < 20;
	wire        io_busy = phase >= 20 && phase < 25;
	always @(posedge clk) c <= c + 1;

	integer     hx = 0, vy = 0;
	wire        hbl_n = hx < H_ACT;
	wire        vbl_n = vy < V_ACT;
	wire [23:0] rgb_in = {8'(hx), 8'(vy * 3), 8'h80};
	wire [23:0] rgb_out;
	always @(posedge vclk) begin
		if (hx == H_TOT - 1) begin
			hx <= 0;
			vy <= vy == V_TOT - 1 ? 0 : vy + 1;
		end
		else hx <= hx + 1;
	end

	P3DO_PAUSE_METER dut (
		.CLK(clk), .PAUSE(pause), .DDR_BUSY(ddr_busy), .VRAM_WAIT(vram_wait), .IO_BUSY(io_busy), .ENABLE(enable),
		.VCLK(vclk), .CE_PIX(1'b1), .HBL_N(hbl_n), .VBL_N(vbl_n), .RGB_IN(rgb_in), .RGB_OUT(rgb_out)
	);

	// Picture of the current frame, sampled at each pixel
	reg  [23:0] pic [0:V_ACT-1][0:H_ACT-1];
	reg  [23:0] src [0:V_ACT-1][0:H_ACT-1];
	always @(negedge vclk) begin
		if (hbl_n && vbl_n) begin
			pic[vy][hx] = rgb_out;
			src[vy][hx] = rgb_in;
		end
	end

	task automatic wait_frames(input int n);
		repeat (n) begin
			@(posedge vclk iff (vy == V_TOT - 1 && hx == H_TOT - 1));
		end
		@(posedge vclk);
	endtask

	function automatic bit in_box(input int x, input int y);
		return x >= 15 && x <= 272 && y >= 23 && y < 55;
	endfunction

	integer errors = 0;
	task automatic fail(input string why);
		$display("TB FAIL %s", why);
		errors++;
	endtask

	initial begin
		int len, expect_len[4];
		reg [23:0] colors[4];
		integer f;
		colors = '{24'hFF4040, 24'hFFD000, 24'h00D0FF, 24'h40FF40};
		expect_len = '{64, 25, 25, 12};

		// Meter on: after a few frames the bars reflect the last full frame
		enable = 1;
		wait_frames(4);
		for (int r = 0; r < 4; r++) begin
			for (int line = 0; line < 6; line++) begin
				len = 0;
				for (int x = 16; x < 272; x++) if (pic[24 + 8 * r + line][x] == colors[r]) len++;
				if (len < expect_len[r] - 1 || len > expect_len[r] + 1)
					fail($sformatf("bar %0d line %0d: %0d pixels, expected %0d", r + 1, line, len, expect_len[r]));
			end
			$display("TB BAR %0d: %0d pixels (expected %0d)", r + 1, len, expect_len[r]);
		end
		for (int y = 0; y < V_ACT; y++)
			for (int x = 0; x < H_ACT; x++) begin
				if (!in_box(x, y) && pic[y][x] != src[y][x]) fail($sformatf("pixel %0d,%0d changed outside the meter", x, y));
				if (in_box(x, y) && pic[y][x] == src[y][x]) fail($sformatf("pixel %0d,%0d of the meter not drawn", x, y));
			end
		f = $fopen("meter.ppm", "w");
		$fwrite(f, "P3\n%0d %0d\n255\n", H_ACT, V_ACT);
		for (int y = 0; y < V_ACT; y++)
			for (int x = 0; x < H_ACT; x++)
				$fwrite(f, "%0d %0d %0d\n", pic[y][x][23:16], pic[y][x][15:8], pic[y][x][7:0]);
		$fclose(f);

		// Meter off: the picture goes through unchanged
		enable = 0;
		wait_frames(2);
		for (int y = 0; y < V_ACT; y++)
			for (int x = 0; x < H_ACT; x++)
				if (pic[y][x] != src[y][x]) fail($sformatf("pixel %0d,%0d changed with the meter off", x, y));

		if (errors == 0) $display("TB PASS");
		else $display("TB FAIL %0d errors", errors);
		$finish;
	end

	initial begin
		#50_000_000;
		$display("TB FAIL timeout");
		$finish;
	end

endmodule
