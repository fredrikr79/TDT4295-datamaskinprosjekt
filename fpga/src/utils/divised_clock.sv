module divised_clock #(
    parameter int unsigned N = 2,
    localparam int unsigned CounterWidth = N > 1 ? $clog2(N) : 1
) (
    input  logic old_clock,
    input  logic reset,
    input  logic enable,
    output logic new_clock
);
  logic [CounterWidth-1:0] count;

  mod_n_counter #(
      .N(N),
      .AT_POSEDGE(1),
      .AT_NEGEDGE(1)
  ) counter (
      .clk(old_clock),
      .reset(reset),
      .enable(enable),
      .count(count)
  );

  always @(posedge old_clock or negedge old_clock) begin
    if (reset) begin
      new_clock <= 0;
    end else if (count == 0) begin
      new_clock <= ~new_clock;
    end
  end

endmodule
