/* Equivalent hand-written ready/valid RTL for fixed_point_mac.pigen.
 * It is intentionally explicit so the comparison shows the wiring Pigen owns. */
module fixed_point_mac_vanilla
	(
		input logic clk,
		input logic reset,
		input logic [15:0] sample,
		input logic sample_valid,
		output logic sample_ready,
		input logic [15:0] coefficient,
		input logic coefficient_valid,
		output logic coefficient_ready,
		input logic [15:0] bias,
		input logic bias_valid,
		output logic bias_ready,
		output logic [15:0] result,
		output logic result_valid,
		input logic result_ready
	);

	logic [31:0] product;
	logic product_valid;
	logic product_in_ready;
	logic product_out_ready;
	logic result_in_ready;

	assign result_in_ready = !result_valid || result_ready;
	assign product_out_ready = result_in_ready && bias_valid;
	assign product_in_ready = !product_valid || product_out_ready;

	assign sample_ready = product_in_ready && coefficient_valid;
	assign coefficient_ready = product_in_ready && sample_valid;
	assign bias_ready = result_in_ready && product_valid;

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			product_valid <= 1'b0;
			product <= '0;
		end
		else if (sample_valid && coefficient_valid && product_in_ready)
		begin
			product_valid <= 1'b1;
			product <= sample * coefficient;
		end
		else if (product_valid && product_out_ready)
			product_valid <= 1'b0;
	end

	always_ff @(posedge clk)
	begin
		if (reset)
		begin
			result_valid <= 1'b0;
			result <= '0;
		end
		else if (product_valid && bias_valid && result_in_ready)
		begin
			result_valid <= 1'b1;
			result <= product[23:8] + bias;
		end
		else if (result_valid && result_ready)
			result_valid <= 1'b0;
	end
endmodule
