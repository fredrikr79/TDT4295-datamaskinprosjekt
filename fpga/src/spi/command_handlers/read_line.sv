module read_line #(
    logic [7:0] N_BYTES = config_pkg::activeWidth
) (
    input wire clk,
    ck_rst,
    input logic [7:0] buffer,
    output logic [7:0] read_line_fifo,
    output logic read_line_done,
    read_line_r_en
);

  logic [7:0] rx_index = 8'd0;
  logic read_line_fifo = 8'd0;

  always_ff @(posedge clk) begin

    if (!ck_rst) begin
      rx_index <= 8'd0;
      read_line_done <= 1'b0;
      read_line_w_en <= 1'b0;
      read_out_fifo <= 8'd0;
    end else begin
      read_line_done <= 1'b0;
      read_line_w_en <= 1'b0;

      if (rx_index < N_BYTES) begin
        write_line_fifo <= buffer;
        read_line_w_en <= 1'b1;
        tx_index <= tx_index + 8'd1;
      end else read_line_done <= 1'b1;
    end
  end
endmodule
