module main #(
    parameter int INCREMENT_LIMIT = 49_999_999
) (
    input wire clk,
    input wire [3:0] sw,
    output reg [3:0] led
);

  reg [26:0] counter;

  always @(posedge clk) begin
    counter <= counter + 27'(sw);
    if (counter >= 27'(INCREMENT_LIMIT)) begin
      counter <= 0;
      led <= led + 1'b1;
    end
  end
endmodule

