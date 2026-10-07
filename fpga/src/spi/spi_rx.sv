module spi_rx #(
) (
    input wire clk,
    ck_sck,
    ck_ss,
    ck_ss_s,
    reset,
    reset_s,
    in_r_en,
    input wire [7:0] octo_spi,

    input spi_pck::state_e state,

    output wire acknack,
    in_empty,
    in_ready,
    in_full,
    output logic rx_r_en,
    output wire [7:0] in_fifo
);

  import spi_pck::*;

  // MCU WRITE CONDITIONS
  assign in_w_en = rx_event_trigger && ~in_full;

  // RX signals
  wire in_w_en;
  wire in_almost_empty;
  wire in_wr_rst_busy;
  wire in_rd_rst_busy;
  wire in_wr_ack;

  // Event driven RX Capture
  wire rx_wr_gate = (~ck_ss) && ((state == ST_READY) | (state == ST_RX) | (state == ST_TX_IDLE));
  wire rx_event_trigger = rx_event_sync[2] ^ rx_event_sync[1];

  reg [7:0] rx_capture = 8'h00;
  reg rx_event = 1'b0;
  reg [2:0] rx_event_sync = 3'b000;

  always @(posedge clk) begin
    if (reset_s) rx_event_sync <= 3'b000;
    else rx_event_sync <= {rx_event_sync[1:0], rx_event};
  end

  assign in_ready = ~in_wr_rst_busy && ~in_rd_rst_busy && ~in_full;

  in_buffer in_queue (
      .din(rx_capture),
      .dout(in_fifo),
      .almost_full(in_almost_full),
      .full(in_full),
      .almost_empty(in_almost_empty),
      .empty(in_empty),
      .wr_en(in_w_en),
      .rd_en(in_r_en),
      .wr_ack(in_wr_ack),
      .rst(reset_s),
      .wr_clk(clk),
      .rd_clk(clk),
      .wr_rst_busy(in_wr_rst_busy),
      .rd_rst_busy(in_rd_rst_busy)
  );

  // RX data retrieval logic
    always_ff @(posedge clk) begin
        if (reset_s) begin
            rx_r_en <= 1'b0;
        end else begin
            rx_r_en <= 1'b0;

            if (state == ST_RX && ck_ss_s && !in_empty) begin
                rx_r_en <= 1'b1;
            end
        end
    end

  // CDC synchronizations CLK -> CK_SCK
  always @(posedge ck_sck) begin
    if (reset_s) begin
      rx_event   <= 1'b0;
      rx_capture <= 8'h00;
    end else begin

      if (rx_wr_gate) begin
        rx_capture <= octo_spi;
        rx_event   <= ~rx_event;
      end
    end
  end

  // ACK logic
  reg [2:0] ack_sync = 3'b000;
  always @(posedge clk) begin
    if (reset) begin
      ack_sync <= 3'b000;
    end else ack_sync <= {ack_sync[1:0], in_wr_ack};
  end

  assign acknack = ack_sync[2];

endmodule
