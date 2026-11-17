/*
 * Pigen signal storage primitives.
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

// One outstanding write and read per target. Independent ownership permits
// W before AW and keeps the complete write burst associated with its B route.
// Retained AXI fabric primitive; compiler fabric generation is not in use.
module pigen_axi_merge #(
	parameter integer N=2, AW_W=1, W_W=1, B_W=1, AR_W=1, R_W=1,
	parameter integer FULL=0, WLAST_BIT=0, RLAST_BIT=0
) (
	input logic clk, reset,
	input logic [N*AW_W-1:0] s_aw_payload,
	input logic [N-1:0] s_aw_valid,
	output logic [N-1:0] s_aw_ready,
	input logic [N*W_W-1:0] s_w_payload,
	input logic [N-1:0] s_w_valid,
	output logic [N-1:0] s_w_ready,
	output logic [N*B_W-1:0] s_b_payload,
	output logic [N-1:0] s_b_valid,
	input logic [N-1:0] s_b_ready,
	input logic [N*AR_W-1:0] s_ar_payload,
	input logic [N-1:0] s_ar_valid,
	output logic [N-1:0] s_ar_ready,
	output logic [N*R_W-1:0] s_r_payload,
	output logic [N-1:0] s_r_valid,
	input logic [N-1:0] s_r_ready,
	output logic [AW_W-1:0] m_aw_payload,
	output logic m_aw_valid,
	input logic m_aw_ready,
	output logic [W_W-1:0] m_w_payload,
	output logic m_w_valid,
	input logic m_w_ready,
	input logic [B_W-1:0] m_b_payload,
	input logic m_b_valid,
	output logic m_b_ready,
	output logic [AR_W-1:0] m_ar_payload,
	output logic m_ar_valid,
	input logic m_ar_ready,
	input logic [R_W-1:0] m_r_payload,
	input logic m_r_valid,
	output logic m_r_ready
);
	logic write_active, read_active, aw_done, w_done, ar_done;
	integer unsigned write_owner, read_owner, write_last, read_last;
	integer unsigned write_choice, read_choice;
	always_comb begin
		write_choice=N; read_choice=N;
		for(integer unsigned step=1; step<=N; step++) begin
			if(write_choice==N && (s_aw_valid[(write_last+step)%N] || s_w_valid[(write_last+step)%N]))
				write_choice=(write_last+step)%N;
			if(read_choice==N && s_ar_valid[(read_last+step)%N]) read_choice=(read_last+step)%N;
		end
		m_aw_payload=s_aw_payload[write_owner*AW_W +: AW_W];
		m_w_payload=s_w_payload[write_owner*W_W +: W_W];
		m_ar_payload=s_ar_payload[read_owner*AR_W +: AR_W];
		m_aw_valid=write_active && !aw_done && s_aw_valid[write_owner];
		m_w_valid=write_active && !w_done && s_w_valid[write_owner];
		m_ar_valid=read_active && !ar_done && s_ar_valid[read_owner];
		s_aw_ready='0; s_w_ready='0; s_b_valid='0; s_b_payload='0;
		s_ar_ready='0; s_r_valid='0; s_r_payload='0;
		m_b_ready=1'b0; m_r_ready=1'b0;
		if(write_active) begin
			s_aw_ready[write_owner]=!aw_done && m_aw_ready;
			s_w_ready[write_owner]=!w_done && m_w_ready;
			if(aw_done && w_done) begin
				s_b_payload[write_owner*B_W +: B_W]=m_b_payload;
				s_b_valid[write_owner]=m_b_valid;
				m_b_ready=s_b_ready[write_owner];
			end
		end
		if(read_active) begin
			s_ar_ready[read_owner]=!ar_done && m_ar_ready;
			if(ar_done) begin
				s_r_payload[read_owner*R_W +: R_W]=m_r_payload;
				s_r_valid[read_owner]=m_r_valid;
				m_r_ready=s_r_ready[read_owner];
			end
		end
	end
	always_ff @(posedge clk) begin
		if(reset) begin
			write_active<=0; read_active<=0; aw_done<=0; w_done<=0; ar_done<=0;
			write_owner<=0; read_owner<=0; write_last<=N-1; read_last<=N-1;
		end else begin
			if(!write_active) begin
				if(write_choice!=N) begin write_active<=1; write_owner<=write_choice; aw_done<=0; w_done<=0; end
			end else begin
				if(m_aw_valid && m_aw_ready) aw_done<=1;
				if(m_w_valid && m_w_ready && (FULL==0 || m_w_payload[WLAST_BIT])) w_done<=1;
				if(m_b_valid && m_b_ready) begin write_active<=0; write_last<=write_owner; end
			end
			if(!read_active) begin
				if(read_choice!=N) begin read_active<=1; read_owner<=read_choice; ar_done<=0; end
			end else begin
				if(m_ar_valid && m_ar_ready) ar_done<=1;
				if(m_r_valid && m_r_ready && (FULL==0 || m_r_payload[RLAST_BIT])) begin read_active<=0; read_last<=read_owner; end
			end
		end
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

module pigen_merge2 #(parameter type PAYLOAD_T = logic) (
	input  logic clk,
	input  logic reset,
	input  logic i0_valid,
	output logic i0_ready,
	input  PAYLOAD_T packet_in0,
	input  logic i1_valid,
	output logic i1_ready,
	input  PAYLOAD_T packet_in1,
	output logic out_valid,
	input  logic out_ready,
	output PAYLOAD_T packet_out
);
	logic last;
	wire grant0 = i0_valid && (last || !i1_valid);
	wire grant1 = i1_valid && (!last || !i0_valid);
	wire fire = out_valid && out_ready;

	assign out_valid = i0_valid || i1_valid;
	assign packet_out = grant0 ? packet_in0 : packet_in1;
	assign i0_ready = grant0 && out_ready;
	assign i1_ready = grant1 && out_ready;

	always_ff @(posedge clk) begin
		if (reset) last <= 1'b0;
		else if (fire) last <= grant1;
	end
endmodule

module pigen_merge #(parameter type PAYLOAD_T = logic, parameter integer N = 2) (
	input  logic clk,
	input  logic reset,
	input  logic [N-1:0] in_valid,
	output logic [N-1:0] in_ready,
	input  logic [N*$bits(PAYLOAD_T)-1:0] packet_in,
	output logic out_valid,
	input  logic out_ready,
	output PAYLOAD_T packet_out
);
	localparam integer W = $bits(PAYLOAD_T);
	localparam integer IW = (N <= 1) ? 1 : $clog2(N);
	logic [IW-1:0] last;
	logic [IW-1:0] chosen;
	logic [N-1:0] grant;
	always @* begin
		integer k;
		chosen = '0;
		out_valid = 1'b0;
		grant = '0;
		packet_out = '0;
		for (k = 0; k < N; k = k + 1) begin
			integer idx;
			idx = (last + 1 + k) % N;
			if (!out_valid && in_valid[idx]) begin
				out_valid = 1'b1;
				chosen = idx;
				packet_out = packet_in[idx*W +: W];
				grant[idx] = 1'b1;
			end
		end
	end
	assign in_ready = grant & {N{out_ready}};
	always_ff @(posedge clk) begin
		if (reset) last <= '0;
		else if (out_valid && out_ready) last <= chosen;
	end
endmodule

// Native stream arbitration keeps a packet's source until accepted TLAST.
// Per-source queues isolate both interface directions from arbitration.
// Switched graphs disable packet locks: interleaved destinations can otherwise
// form cycles of grants waiting for each other's packet tails.
// Retained AXI fabric primitive; compiler fabric generation is not in use.
module pigen_axis_merge #(
	parameter type PAYLOAD_T = logic,
	parameter integer N = 2,
	parameter integer LAST_BIT = 0,
	parameter integer HAS_LAST = 0,
	parameter integer PACKET_LOCK = 1,
	parameter integer TRACK_STREAMS = 0,
	parameter integer ID_LSB = 0,
	parameter integer ID_W = 0,
	parameter integer DEST_LSB = 0,
	parameter integer DEST_W = 0
) (
	input logic clk, reset,
	input logic [N-1:0] in_valid,
	output logic [N-1:0] in_ready,
	input logic [N*$bits(PAYLOAD_T)-1:0] packet_in,
	output logic out_valid,
	input logic out_ready,
	output PAYLOAD_T packet_out
);
	localparam integer W = $bits(PAYLOAD_T);
	logic [N-1:0] held_valid, held_ready;
	logic [N*W-1:0] held_packet;
	logic [W-1:0] selected_packet;
	// Exact-width fan-in cannot mix origins while any interleaved packet is
	// open. The namespace bitmap is bounded by the compiler's bit policy.
	localparam integer KEY_W = TRACK_STREAMS != 0 && ID_W + DEST_W > 0 ? ID_W + DEST_W : 1;
	localparam longint unsigned STREAMS = TRACK_STREAMS != 0 ? (64'd1 << (ID_W + DEST_W)) : 64'd1;
	localparam integer COUNT_W = $clog2(STREAMS+1) > 0 ? $clog2(STREAMS+1) : 1;
	logic [STREAMS-1:0] open_streams;
	logic [COUNT_W-1:0] open_count;
	logic [KEY_W-1:0] stream_key;
	wire release_packet = PACKET_LOCK == 0 || HAS_LAST == 0 ||
		(selected_packet[LAST_BIT] && (TRACK_STREAMS == 0 || open_count == 0 ||
		(open_count == 1 && open_streams[stream_key])));
	if (TRACK_STREAMS != 0) begin : packet_identity
		always_comb begin
			stream_key='0;
			for (integer bit_index=0;bit_index<ID_W;bit_index++) stream_key[bit_index]=selected_packet[ID_LSB+bit_index];
			for (integer bit_index=0;bit_index<DEST_W;bit_index++) stream_key[ID_W+bit_index]=selected_packet[DEST_LSB+bit_index];
		end
	end else begin : no_packet_identity
		assign stream_key='0;
	end
	always_ff @(posedge clk) begin
		if (reset) begin open_streams<='0;open_count<='0;end
		else if (TRACK_STREAMS != 0 && out_valid && out_ready) begin
			if (selected_packet[LAST_BIT]) begin
				if (open_streams[stream_key]) begin
					open_streams[stream_key]<=1'b0;open_count<=open_count-1'b1;
				end
			end else if (!open_streams[stream_key]) begin
				open_streams[stream_key]<=1'b1;open_count<=open_count+1'b1;
			end
		end
	end
	logic active;
	integer unsigned owner, last, chosen;
	for (genvar lane = 0; lane < N; lane++) begin : inputs_buffer
		logic [1:0] count;
		logic [W-1:0] first_packet, second_packet;
		wire push = in_valid[lane] && in_ready[lane];
		wire pop = held_valid[lane] && held_ready[lane];
		assign in_ready[lane] = count != 2;
		assign held_valid[lane] = count != 0;
		assign held_packet[lane*W +: W] = first_packet;
		always_ff @(posedge clk) begin
			if (reset) begin count <= 0; first_packet <= '0; second_packet <= '0; end
			else begin
				case ({push,pop})
					2'b10: count <= count + 1'b1;
					2'b01: count <= count - 1'b1;
					default: begin end
				endcase
				if (push) begin
					if (count == 0 || (pop && count == 1)) first_packet <= packet_in[lane*W +: W];
					else second_packet <= packet_in[lane*W +: W];
				end
				if (pop && count == 2) first_packet <= second_packet;
			end
		end
	end
	always_comb begin
		chosen = N;
		for (integer unsigned step = 1; step <= N; step++) begin
			if (chosen == N && held_valid[(last + step) % N]) chosen = (last + step) % N;
		end
		out_valid = active && held_valid[owner];
		selected_packet = held_packet[owner*W +: W];
		packet_out = PAYLOAD_T'(selected_packet);
		held_ready = '0;
		if (active) held_ready[owner] = out_ready;
	end
	always_ff @(posedge clk) begin
		if (reset) begin
			active <= 1'b0;
			owner <= 0;
			last <= N-1;
		end else if (!active) begin
			if (chosen != N) begin owner <= chosen; active <= 1'b1; end
		end else if (out_valid && out_ready && release_packet) begin
			active <= 1'b0;
			last <= owner;
		end
	end
endmodule

// Route complete Stream beats by TDEST. Unmapped destinations retain the head
// beat and propagate backpressure; reset discards queued beats synchronously.
// Retained AXI fabric primitive; compiler fabric generation is not in use.
module pigen_axis_demux #(
	parameter type PAYLOAD_T = logic,
	parameter integer N = 2,
	parameter integer DEST_LSB = 0,
	parameter integer DEST_W = 1
) (
	input logic clk, reset,
	input logic in_valid,
	output logic in_ready,
	input PAYLOAD_T packet_in,
	output logic [N-1:0] out_valid,
	input logic [N-1:0] out_ready,
	output logic [N*$bits(PAYLOAD_T)-1:0] packet_out
);
	localparam integer W = $bits(PAYLOAD_T);
	logic [W-1:0] held_packet;
	logic held_valid, held_ready;
	logic [1:0] count;
	logic read_pointer, write_pointer;
	PAYLOAD_T packets [0:1];
	wire push = in_valid && in_ready;
	wire pop = held_valid && held_ready;
	assign in_ready = count != 2;
	assign held_valid = count != 0;
	assign held_packet = packets[read_pointer];
	always_ff @(posedge clk) begin
		if (reset) begin
			count <= 0; read_pointer <= 0; write_pointer <= 0;
		end else begin
			if (push) begin packets[write_pointer] <= packet_in; write_pointer <= ~write_pointer; end
			if (pop) read_pointer <= ~read_pointer;
			case ({push,pop})
				2'b10: count <= count + 1'b1;
				2'b01: count <= count - 1'b1;
				default: begin end
			endcase
		end
	end
	for (genvar lane = 0; lane < N; lane++) begin : routes
		assign out_valid[lane] = held_valid && held_packet[DEST_LSB +: DEST_W] == DEST_W'(lane);
		assign packet_out[lane*W +: W] = held_packet;
	end
	assign held_ready = |(out_valid & out_ready);
	initial begin
		if (N < 1 || DEST_W < 1 || DEST_LSB < 0 || DEST_LSB + DEST_W > W)
			$fatal(1,"invalid AXI-Stream demultiplexer shape");
		if (DEST_W < 31 && N > (1 << DEST_W))
			$fatal(1,"AXI-Stream demultiplexer needs sufficient TDEST bits");
	end
endmodule

// One source, independently owned write/read transactions, address windows.
// Requests keep their original addresses; unmapped transactions complete locally.
// Retained AXI fabric primitive; compiler fabric generation is not in use.
module pigen_axi_demux #(
	parameter integer N=1, AW_W=1, W_W=1, B_W=2, AR_W=1, R_W=2,
	parameter integer ADDR_W=1, AW_ADDR_LSB=0, AR_ADDR_LSB=0,
	parameter integer FULL=0, WLAST_BIT=0, RLAST_BIT=0, ARLEN_LSB=0,
	parameter integer ID_W=0, AW_ID_LSB=0, AR_ID_LSB=0, B_ID_LSB=0, R_ID_LSB=0,
	parameter integer B_RESP_LSB=0, R_RESP_LSB=0,
	parameter logic [N*ADDR_W-1:0] BASES='0, LIMITS='1
) (
	input logic clk, reset,
	input logic [AW_W-1:0] s_aw_payload, input logic s_aw_valid, output logic s_aw_ready,
	input logic [W_W-1:0] s_w_payload, input logic s_w_valid, output logic s_w_ready,
	output logic [B_W-1:0] s_b_payload, output logic s_b_valid, input logic s_b_ready,
	input logic [AR_W-1:0] s_ar_payload, input logic s_ar_valid, output logic s_ar_ready,
	output logic [R_W-1:0] s_r_payload, output logic s_r_valid, input logic s_r_ready,
	output logic [N*AW_W-1:0] m_aw_payload, output logic [N-1:0] m_aw_valid, input logic [N-1:0] m_aw_ready,
	output logic [N*W_W-1:0] m_w_payload, output logic [N-1:0] m_w_valid, input logic [N-1:0] m_w_ready,
	input logic [N*B_W-1:0] m_b_payload, input logic [N-1:0] m_b_valid, output logic [N-1:0] m_b_ready,
	output logic [N*AR_W-1:0] m_ar_payload, output logic [N-1:0] m_ar_valid, input logic [N-1:0] m_ar_ready,
	input logic [N*R_W-1:0] m_r_payload, input logic [N-1:0] m_r_valid, output logic [N-1:0] m_r_ready
);
	function automatic integer unsigned decode(input logic [ADDR_W-1:0] address);
		decode=N;
		for(integer unsigned lane=0;lane<N;lane++)
			if(address>=BASES[lane*ADDR_W +: ADDR_W] && address<=LIMITS[lane*ADDR_W +: ADDR_W]) decode=lane;
	endfunction
	logic write_active,read_active,write_error,read_error,aw_done,w_done,ar_done;
	integer unsigned write_owner,read_owner,aw_choice,ar_choice;
	logic [AW_W-1:0] aw_saved;
	logic [AR_W-1:0] ar_saved;
	logic [8:0] read_remaining;
	wire [7:0] request_length;
	if(FULL!=0) begin : full_length
		assign request_length=s_ar_payload[ARLEN_LSB +: 8];
	end else begin : lite_length
		assign request_length=0;
	end
	logic [W_W-1:0] w_packets[0:1];
	logic [1:0] w_count;
	logic w_read,w_write,pop_w;
	wire push_w=s_w_valid && s_w_ready;
	wire [W_W-1:0] w_head=w_packets[w_read];
	assign s_w_ready=w_count!=2;
	assign s_aw_ready=!write_active;
	assign s_ar_ready=!read_active;
	always_ff @(posedge clk) begin
		if(reset) begin w_count<=0;w_read<=0;w_write<=0;end
		else begin
			if(push_w) begin w_packets[w_write]<=s_w_payload;w_write<=~w_write;end
			if(pop_w) w_read<=~w_read;
			case({push_w,pop_w})
				2'b10:w_count<=w_count+1'b1;
				2'b01:w_count<=w_count-1'b1;
				default:begin end
			endcase
		end
	end
	always_comb begin
		aw_choice=decode(s_aw_payload[AW_ADDR_LSB +: ADDR_W]);
		ar_choice=decode(s_ar_payload[AR_ADDR_LSB +: ADDR_W]);
		for(integer lane=0;lane<N;lane++) begin
			m_aw_payload[lane*AW_W +: AW_W]=aw_saved;
			m_w_payload[lane*W_W +: W_W]=w_head;
			m_ar_payload[lane*AR_W +: AR_W]=ar_saved;
		end
		m_aw_valid='0;m_w_valid='0;m_b_ready='0;m_ar_valid='0;m_r_ready='0;
		s_b_valid=0;s_b_payload='0;s_r_valid=0;s_r_payload='0;pop_w=0;
		if(write_active) begin
			if(write_error) begin
				pop_w=!w_done && w_count!=0;
				s_b_valid=w_done;
				s_b_payload[B_RESP_LSB +: 2]=2'b11;
				for(integer bitno=0;bitno<ID_W;bitno++) s_b_payload[B_ID_LSB+bitno]=aw_saved[AW_ID_LSB+bitno];
			end else begin
				m_aw_valid[write_owner]=!aw_done;
				m_w_valid[write_owner]=!w_done && w_count!=0;
				pop_w=m_w_valid[write_owner] && m_w_ready[write_owner];
				if(aw_done && w_done) begin
					s_b_payload=m_b_payload[write_owner*B_W +: B_W];
					s_b_valid=m_b_valid[write_owner];m_b_ready[write_owner]=s_b_ready;
				end
			end
		end
		if(read_active) begin
			if(read_error) begin
				s_r_valid=1;
				s_r_payload[R_RESP_LSB +: 2]=2'b11;
				for(integer bitno=0;bitno<ID_W;bitno++) s_r_payload[R_ID_LSB+bitno]=ar_saved[AR_ID_LSB+bitno];
				if(FULL!=0) s_r_payload[RLAST_BIT]=read_remaining==1;
			end else begin
				m_ar_valid[read_owner]=!ar_done;
				if(ar_done) begin
					s_r_payload=m_r_payload[read_owner*R_W +: R_W];
					s_r_valid=m_r_valid[read_owner];m_r_ready[read_owner]=s_r_ready;
				end
			end
		end
	end
	always_ff @(posedge clk) begin
		if(reset) begin
			write_active<=0;read_active<=0;write_error<=0;read_error<=0;
			aw_done<=0;w_done<=0;ar_done<=0;write_owner<=0;read_owner<=0;
			aw_saved<='0;ar_saved<='0;read_remaining<=0;
		end else begin
			if(s_aw_valid && s_aw_ready) begin
				write_active<=1;write_owner<=aw_choice;write_error<=aw_choice==N;
				aw_saved<=s_aw_payload;aw_done<=aw_choice==N;w_done<=0;
			end else if(write_active) begin
				if(!write_error && m_aw_valid[write_owner] && m_aw_ready[write_owner]) aw_done<=1;
				if(pop_w && (FULL==0 || w_head[WLAST_BIT])) w_done<=1;
				if(s_b_valid && s_b_ready) write_active<=0;
			end
			if(s_ar_valid && s_ar_ready) begin
				read_active<=1;read_owner<=ar_choice;read_error<=ar_choice==N;
				ar_saved<=s_ar_payload;ar_done<=ar_choice==N;
				read_remaining<={1'b0,request_length}+9'd1;
			end else if(read_active) begin
				if(!read_error && m_ar_valid[read_owner] && m_ar_ready[read_owner]) ar_done<=1;
				if(s_r_valid && s_r_ready) begin
					if(read_error) read_remaining<=read_remaining-1'b1;
					if(FULL==0 || (read_error ? read_remaining==1 : s_r_payload[RLAST_BIT])) read_active<=0;
				end
			end
		end
	end
	initial begin
		if(N<1 || ADDR_W<1 || AW_W<1 || W_W<1 || AR_W<1 || B_W<2 || R_W<2 || ID_W<0 ||
			AW_ADDR_LSB<0 || AW_ADDR_LSB+ADDR_W>AW_W || AR_ADDR_LSB<0 || AR_ADDR_LSB+ADDR_W>AR_W ||
			B_RESP_LSB<0 || B_RESP_LSB+2>B_W || R_RESP_LSB<0 || R_RESP_LSB+2>R_W)
			$fatal(1,"invalid AXI demultiplexer layout");
		if(FULL!=0 && (WLAST_BIT<0 || WLAST_BIT>=W_W || RLAST_BIT<0 || RLAST_BIT>=R_W || ARLEN_LSB<0 || ARLEN_LSB+8>AR_W))
			$fatal(1,"invalid full AXI demultiplexer layout");
		if(ID_W>0 && (AW_ID_LSB<0 || AW_ID_LSB+ID_W>AW_W || AR_ID_LSB<0 || AR_ID_LSB+ID_W>AR_W ||
			B_ID_LSB<0 || B_ID_LSB+ID_W>B_W || R_ID_LSB<0 || R_ID_LSB+ID_W>R_W))
			$fatal(1,"invalid AXI demultiplexer ID layout");
		for(integer lane=0;lane<N;lane++) begin
			if(BASES[lane*ADDR_W +: ADDR_W]>LIMITS[lane*ADDR_W +: ADDR_W]) $fatal(1,"reversed AXI address window");
			for(integer other=0;other<lane;other++)
				if(BASES[lane*ADDR_W +: ADDR_W]<=LIMITS[other*ADDR_W +: ADDR_W] && BASES[other*ADDR_W +: ADDR_W]<=LIMITS[lane*ADDR_W +: ADDR_W])
					$fatal(1,"overlapping AXI address windows");
		end
	end
endmodule

// Three-port source-routed node. Route bits never cross a user module boundary.
module pigen_tree_router #(
 parameter type PAYLOAD_T=logic[1:0],
 parameter integer PATH_W=1,
 parameter integer W=$bits(PAYLOAD_T), DATA_W=W-PATH_W
)(
 input logic clk, reset,
 input logic[2:0] in_valid,
 output logic[2:0] in_ready,
 input logic[3*W-1:0] packet_in,
 output logic[2:0] out_valid,
 input logic[2:0] out_ready,
 output logic[3*W-1:0] packet_out
);
 logic[1:0] count[3];
 PAYLOAD_T head[3], tail[3];
 wire[2:0] available;
 wire[2:0] accepted[3];
 wire[2:0] remove;
 function automatic PAYLOAD_T forward_packet(input PAYLOAD_T packet);
  logic[PATH_W-1:0] path;
  path=packet[W-1:DATA_W]>>1;
  path[PATH_W-1]=packet[DATA_W];
  return {path,packet[DATA_W-1:0]};
 endfunction
 for(genvar i=0;i<3;i++) begin: ingress
  assign in_ready[i]=(count[i]!=2);
  assign available[i]=(count[i]!=0);
  assign remove[i]=accepted[0][i]|accepted[1][i]|accepted[2][i];
  wire push=in_valid[i]&&in_ready[i];
  always_ff @(posedge clk) begin
   if(reset) begin count[i]<=0;head[i]<='0;tail[i]<='0;end
   else case({push,remove[i]})
    2'b10: begin if(count[i]==0)head[i]<=packet_in[i*W+:W];else tail[i]<=packet_in[i*W+:W];count[i]<=count[i]+1'b1;end
    2'b01: begin if(count[i]==2)head[i]<=tail[i];count[i]<=count[i]-1'b1;end
    2'b11: begin if(count[i]==2)begin head[i]<=tail[i];tail[i]<=packet_in[i*W+:W];end else head[i]<=packet_in[i*W+:W];end
    default: begin end
   endcase
  end
 end
 for(genvar o=0;o<3;o++) begin: egress
  localparam integer A=(o+2)%3, B=(o+1)%3;
  wire request_a=available[A]&&!head[A][DATA_W];
  wire request_b=available[B]&&head[B][DATA_W];
  logic rr,locked,held_choice;
  wire choice=locked ? held_choice : request_a ? (request_b&&rr) : 1'b1;
  assign out_valid[o]=locked || request_a || request_b;
  assign packet_out[o*W+:W]=out_valid[o] ? forward_packet(choice ? head[B]:head[A]) : '0;
  assign accepted[o]=out_valid[o]&&out_ready[o] ? (choice ? (3'b001<<B):(3'b001<<A)) : 3'b000;
  always_ff @(posedge clk) begin
   if(reset) begin rr<=0;locked<=0;held_choice<=0;end
   else if(out_valid[o]) begin
    if(out_ready[o]) begin rr<=!choice;locked<=0;end
    else begin locked<=1;held_choice<=choice;end
   end
  end
 end
endmodule
