module clock_enable_pulse #(
    parameter int unsigned N = 10
) (
    input  logic clk,
    input  logic reset,
    input  logic enable,
    output logic pulse
);

  localparam int unsigned CountWidth = (N > 1) ? $clog2(N) : 1;
  logic [CountWidth-1:0] count;

  mod_n_counter #(
      .N(N)
  ) u_counter (
      .clk   (clk),
      .reset   (reset),
      .enable(enable),
      .count (count)
  );

  always_comb begin
    if ((N > 0) && enable && !reset && (32'(count) == N - 1)) pulse = 1'b1;
    else pulse = 1'b0;
  end

endmodule
