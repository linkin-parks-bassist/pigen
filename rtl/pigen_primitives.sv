/*
 * Pigen transport storage primitives.
 *
 * Generated modules instantiate these directly.  They are ordinary,
 * synthesizable SystemVerilog and intentionally expose the familiar
 * ready/valid/payload shape.  `clear` synchronously discards stored state;
 * normal transfer/clear overlap is rejected by the Pigen frontend.
 */

module pigen_buf #(
	parameter type PAYLOAD_T = logic
)
	(
		input  logic		clk,
		input  logic		reset,
		input  logic		clear,
		input  logic		discard,
		input  logic		force_valid,
		input  logic		force_invalid,
		input  logic		force_after_transfer,

		input  logic		in_valid,
		output logic		in_ready,
		input  PAYLOAD_T	packet_in,

		output logic		out_valid,
		input  logic		out_ready,
		output PAYLOAD_T	packet_out
	);

	logic packet_valid;
	PAYLOAD_T packet;

	assign in_ready = ~packet_valid | out_ready;
	assign out_valid = packet_valid;
	assign packet_out = packet;

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			packet_valid <= force_valid && !force_invalid;
			packet <= force_valid && in_valid ? packet_in : '0;
		end
		else if (clear || discard)
			packet_valid <= 1'b0;
		else if (force_after_transfer && (force_valid || force_invalid))
		begin
			packet_valid <= force_valid;
			if (force_valid && in_valid && in_ready)
				packet <= packet_in;
		end
		else if (in_valid && in_ready)
		begin
			packet_valid <= 1'b1;
			packet <= packet_in;
		end
		else if (out_valid && out_ready)
			packet_valid <= 1'b0;
		else if (force_valid || force_invalid)
			packet_valid <= force_valid;
	end

endmodule

module pigen_fifo #(
	parameter type PAYLOAD_T = logic,
	parameter int DEPTH = 2,
	parameter int POINTER_WIDTH = DEPTH <= 1 ? 1 : $clog2(DEPTH),
	parameter int COUNT_WIDTH = $clog2(DEPTH + 1)
)
	(
		input  logic		clk,
		input  logic		reset,
		input  logic		clear,
		input  logic		discard,
		input  logic		force_valid,
		input  logic		force_invalid,
		input  logic		force_after_transfer,

		input  logic		in_valid,
		output logic		in_ready,
		input  PAYLOAD_T	packet_in,

		output logic		out_valid,
		input  logic		out_ready,
		output PAYLOAD_T	packet_out
	);

	logic [POINTER_WIDTH-1:0] read_pointer;
	logic [POINTER_WIDTH-1:0] write_pointer;
	logic [COUNT_WIDTH-1:0] count;
	localparam logic [POINTER_WIDTH-1:0] LAST_POINTER = POINTER_WIDTH'(DEPTH - 1);
	localparam logic [COUNT_WIDTH-1:0] DEPTH_COUNT = COUNT_WIDTH'(DEPTH);
	PAYLOAD_T packets [0:DEPTH-1];

	logic push;
	logic pop;

	assign out_valid = count != 0;
	/* FIFO occupancy, not downstream combinational readiness, controls input
	 * readiness. This makes every FIFO an explicit ready-chain break. */
	assign in_ready = count != DEPTH_COUNT;
	assign packet_out = packets[read_pointer];

	assign push = in_valid && in_ready;
	assign pop = out_valid && out_ready;

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			read_pointer <= '0;
			write_pointer <= force_valid && !force_invalid && DEPTH > 1 ? POINTER_WIDTH'(1) : '0;
			count <= force_valid && !force_invalid ? 1 : '0;
			if (force_valid && in_valid)
				packets[0] <= packet_in;
		end
		else if (clear || (force_after_transfer && force_invalid))
		begin
			read_pointer <= '0;
			write_pointer <= '0;
			count <= '0;
		end
		else if (discard && out_valid)
		begin
			if (read_pointer == LAST_POINTER)
				read_pointer <= '0;
			else
				read_pointer <= read_pointer + 1'b1;
			count <= count - 1'b1;
		end
		else
		begin
			if (push)
			begin
				packets[write_pointer] <= packet_in;

				if (write_pointer == LAST_POINTER)
					write_pointer <= '0;
				else
					write_pointer <= write_pointer + 1'b1;
			end

			if (pop)
			begin
				if (read_pointer == LAST_POINTER)
					read_pointer <= '0;
				else
					read_pointer <= read_pointer + 1'b1;
			end

			case ({push, pop})
				2'b10: count <= count + 1'b1;
				2'b01: count <= count - 1'b1;
				default: count <= count;
			endcase
			if (force_valid && count == 0)
				count <= 1;
			else if (force_invalid)
				count <= '0;
		end
	end

endmodule

module pigen_skid #(
	parameter type PAYLOAD_T = logic
)
	(
		input  logic		clk,
		input  logic		reset,
		input  logic		clear,
		input  logic		discard,
		input  logic		force_valid,
		input  logic		force_invalid,
		input  logic		force_after_transfer,

		input  logic		in_valid,
		output logic		in_ready,
		input  PAYLOAD_T	packet_in,

		output logic		out_valid,
		input  logic		out_ready,
		output PAYLOAD_T	packet_out
	);

	logic read_pointer;
	logic write_pointer;
	logic [1:0] count;
	PAYLOAD_T packets [0:1];

	logic push;
	logic pop;

	assign out_valid = count != 0;
	/* A skid buffer must register backpressure. Its second slot absorbs the
	 * item already in flight when the registered occupancy reaches full. */
	assign in_ready = count != 2;
	assign packet_out = packets[read_pointer];

	assign push = in_valid && in_ready;
	assign pop = out_valid && out_ready;

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			read_pointer <= 1'b0;
			write_pointer <= force_valid && !force_invalid;
			count <= force_valid && !force_invalid ? 1 : '0;
			if (force_valid && in_valid)
				packets[0] <= packet_in;
		end
		else if (clear || discard || (force_after_transfer && force_invalid))
		begin
			read_pointer <= 1'b0;
			write_pointer <= 1'b0;
			count <= '0;
		end
		else
		begin
			if (push)
			begin
				packets[write_pointer] <= packet_in;
				write_pointer <= ~write_pointer;
			end

			if (pop)
				read_pointer <= ~read_pointer;

			case ({push, pop})
				2'b10: count <= count + 1'b1;
				2'b01: count <= count - 1'b1;
				default: count <= count;
			endcase
			if (force_valid && count == 0)
				count <= 1;
			else if (force_invalid)
				count <= '0;
		end
	end

endmodule

module pigen_port #(
	parameter type PAYLOAD_T = logic
)
	(
		input  logic		clk,
		input  logic		reset,
		input  logic		clear,
		input  logic		discard,
		input  logic		force_valid,
		input  logic		force_invalid,
		input  logic		force_after_transfer,

		input  logic		in_valid,
		output logic		in_ready,
		input  PAYLOAD_T	packet_in,

		output logic		out_valid,
		input  logic		out_ready,
		output PAYLOAD_T	packet_out
	);

	assign in_ready = 1'b1;

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			out_valid <= force_valid && !force_invalid;
			packet_out <= force_valid && in_valid ? packet_in : '0;
		end
		else if (clear || discard)
			out_valid <= 1'b0;
		else if (force_after_transfer && (force_valid || force_invalid))
			out_valid <= force_valid;
		else
		begin
			if (in_valid)
				out_valid <= 1'b1;
			else if (force_valid || force_invalid)
				out_valid <= force_valid;
			else
				out_valid <= 1'b0;

			if (in_valid)
				packet_out <= packet_in;
		end
	end

endmodule
