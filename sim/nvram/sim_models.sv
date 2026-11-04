// Dual-port RAM with different port widths (rtl/bram.vhd, which Verilator cannot read):
// synchronous read, new data on a read during a write on the same port. Enables and chip
// selects are ignored: the save/load logic leaves them at their defaults.
module dpram_dif #(parameter addr_width_a = 8, data_width_a = 8, addr_width_b = 8, data_width_b = 8,
                   parameter mem_init_file = " ")
(
	input                         clock,
	input      [addr_width_a-1:0] address_a,
	input      [data_width_a-1:0] data_a,
	input                         enable_a,
	input                         wren_a,
	output reg [data_width_a-1:0] q_a,
	input                         cs_a,
	input      [addr_width_b-1:0] address_b,
	input      [data_width_b-1:0] data_b,
	input                         enable_b,
	input                         wren_b,
	output reg [data_width_b-1:0] q_b,
	input                         cs_b
);
	localparam int BYTES = (2 ** addr_width_a) * data_width_a / 8;
	reg [7:0] mem [0:BYTES-1];

	always @(posedge clock) begin
		for (int i = 0; i < data_width_a / 8; i++) begin
			if (wren_a) mem[address_a * (data_width_a / 8) + i] <= data_a[i*8 +: 8];
			q_a[i*8 +: 8] <= wren_a ? data_a[i*8 +: 8] : mem[address_a * (data_width_a / 8) + i];
		end
		for (int i = 0; i < data_width_b / 8; i++) begin
			if (wren_b) mem[address_b * (data_width_b / 8) + i] <= data_b[i*8 +: 8];
			q_b[i*8 +: 8] <= wren_b ? data_b[i*8 +: 8] : mem[address_b * (data_width_b / 8) + i];
		end
	end
endmodule
