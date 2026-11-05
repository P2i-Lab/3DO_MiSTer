// hps_io of the MiSTer framework, as seen by the core, driven by the C++ model of Main_MiSTer
// (sim_main.cpp) through hps_cycle(), called on every clk_sys rising edge. Only the ports that
// 3DO.sv connects exist; the core is built with WIDE = 1 (16-bit downloads and SD buffer).
//
// Same timing as the real module where the core can see it:
// - downloads: ioctl_wr is a one-cycle pulse; ioctl_addr/ioctl_dout stay valid until the next
//   word (the address only moves at the next word);
// - EXT_BUS: io_enable, then one io_strobe per 16-bit word, the answer of hps_ext being read
//   after its register;
// - SD buffer: sd_ack around the 256 words of a 512-byte block, sd_buff_wr pulses for reads.

import "DPI-C" function void hps_cycle(
	input  int     ext_dout,
	input  int     sd_rd_i,
	input  int     sd_wr_i,
	input  int     sd_lba_i,
	input  int     sd_buff_din_i,
	output int     ext_din,
	output int     ext_ctrl,     // bit 0 io_strobe, bit 1 io_enable
	output int     dl_ctrl,      // bit 0 ioctl_download, bit 1 ioctl_wr
	output int     dl_index,
	output int     dl_addr,
	output int     dl_data,
	output longint status_lo,
	output longint status_hi,
	output int     joy0,
	output int     sd_ctrl,      // bit 0 sd_ack, bit 1 sd_buff_wr, bit 2 img_mounted
	output int     sd_addr,
	output int     sd_dout
);

module hps_io #(parameter CONF_STR = "", CONF_STR_BRAM = 0, PS2DIV = 0, WIDE = 0, VDNUM = 1, BLKSZ = 2,
                parameter PS2WE = 0, STRLEN = 0, F12KEYMOD = 0, FFB = 0)
(
	input              clk_sys,
	inout       [45:0] HPS_BUS,

	output reg  [31:0] joystick_0,
	output reg  [31:0] joystick_1,
	output reg  [31:0] joystick_2,
	output reg  [31:0] joystick_3,
	output reg  [31:0] joystick_4,
	output reg  [15:0] joystick_l_analog_0,
	output reg  [15:0] joystick_l_analog_1,
	output reg  [15:0] joystick_r_analog_0,
	output reg  [15:0] joystick_r_analog_1,

	output       [1:0] buttons,
	output             forced_scandoubler,
	input              new_vmode,
	inout       [21:0] gamma_bus,

	output reg [127:0] status,
	input      [127:0] status_in,
	input              status_set,
	input       [15:0] status_menumask,

	output reg   [0:0] img_mounted,
	output reg         img_readonly,
	output reg  [63:0] img_size,
	input       [31:0] sd_lba[1],
	input        [0:0] sd_rd,
	input        [0:0] sd_wr,
	output reg   [0:0] sd_ack,
	output reg  [12:0] sd_buff_addr,
	output reg  [15:0] sd_buff_dout,
	input       [15:0] sd_buff_din[1],
	output reg         sd_buff_wr,

	output reg         ioctl_download,
	output reg  [15:0] ioctl_index,
	output reg         ioctl_wr,
	output reg  [26:0] ioctl_addr,
	output reg  [15:0] ioctl_dout,
	input              ioctl_wait,

	output reg  [15:0] sdram_sz,
	output reg  [10:0] ps2_key,
	output reg  [24:0] ps2_mouse,

	inout       [35:0] EXT_BUS
);
	assign buttons = 2'b00;
	assign forced_scandoubler = 1'b0;

	reg [15:0] ext_din_r;
	reg        ext_strobe_r, ext_enable_r;
	assign EXT_BUS[31:16] = ext_din_r;
	assign EXT_BUS[33]    = ext_strobe_r;
	assign EXT_BUS[34]    = ext_enable_r;

	initial begin
		{joystick_1, joystick_2, joystick_3, joystick_4} = '0;
		{joystick_l_analog_0, joystick_l_analog_1, joystick_r_analog_0, joystick_r_analog_1} = '0;
		img_readonly = 0;
		img_size = '0;
		sdram_sz = '0;
		ps2_key = '0;
		ps2_mouse = '0;
	end

	always @(posedge clk_sys) begin
		int e_din, e_ctrl, d_ctrl, d_idx, d_addr, d_data, j0, s_ctrl, s_addr, s_dout;
		longint st_lo, st_hi;

		hps_cycle(int'(EXT_BUS[15:0]), int'(sd_rd[0]), int'(sd_wr[0]), int'(sd_lba[0]), int'(sd_buff_din[0]),
		          e_din, e_ctrl, d_ctrl, d_idx, d_addr, d_data, st_lo, st_hi, j0, s_ctrl, s_addr, s_dout);

		ext_din_r      <= e_din[15:0];
		ext_strobe_r   <= e_ctrl[0];
		ext_enable_r   <= e_ctrl[1];

		ioctl_download <= d_ctrl[0];
		ioctl_wr       <= d_ctrl[1];
		ioctl_index    <= d_idx[15:0];
		ioctl_addr     <= d_addr[26:0];
		ioctl_dout     <= d_data[15:0];

		status         <= {st_hi, st_lo};
		joystick_0     <= j0;

		sd_ack[0]      <= s_ctrl[0];
		sd_buff_wr     <= s_ctrl[1];
		img_mounted[0] <= s_ctrl[2];
		sd_buff_addr   <= s_addr[12:0];
		sd_buff_dout   <= s_dout[15:0];
	end
endmodule
