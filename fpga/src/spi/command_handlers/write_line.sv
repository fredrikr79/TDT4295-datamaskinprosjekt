module write_line #(
    logic [7:0] N_BYTES = config_pkg::activeWidth
) (
    input wire clk,
    ck_rst,
    input logic [7:0] in_fifo,
    output logic write_line_done,
    write_line_r_en,
    output logic [7:0] buffer
);

  logic [7:0] tx_index = 8'd0;

  always_ff @(posedge clk) begin

    if (!ck_rst) begin
      tx_index <= 8'd0;
      write_line_done <= 1'b0;
      write_line_r_en <= 1'b0;
      buffer <= 8'd0;
    end else begin
      write_line_done <= 1'b0;
      write_line_r_en <= 1'b0;

      if (tx_index < N_BYTES) begin
        buffer <= in_fifo;
        write_line_r_en <= 1'b1;
        tx_index <= tx_index + 8'd1;
      end else write_line_done <= 1'b1;
    end
  end
endmodule
