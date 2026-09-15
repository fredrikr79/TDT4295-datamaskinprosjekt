module mod_n_counter #(
    parameter int unsigned N = 10,
    localparam int unsigned WIDTH = (N > 1) ? $clog2(N) : 1
) (
    input  logic             clk,
    input  logic             reset,
    input  logic             enable,
    output logic [WIDTH-1:0] count
);

  initial begin
    assert (N >= 1)
    else $error("N must be greater than or equal to 1");
  end

  always_ff @(posedge clk) begin
    if (reset) begin
      count <= '0;
    end else if (enable) begin
      if (32'(count) == N - 1) count <= '0;
      else count <= count + 1'b1;
    end
  end

endmodule
