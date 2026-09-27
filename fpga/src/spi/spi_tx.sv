module spi_tx #(
) (
    input wire clk,
    ck_sck,
    reset,
    reset_s,
    ck_ss,
    ck_ss_s,
    in_empty,
    out_w_en,
    out_empty_s,

    input spi_pck::state_e state,
    input logic [7:0] out_fifo,
    inout wire [7:0] octo_spi,

    output wire out_empty,
    out_full,
    out_ready,

    output logic tx_r_en,
    tx_w_en
);

  import spi_pck::*;

  // MCU READ CONDITIONS
  assign out_r_en = (tx_load || tx_event_trigger) && ~out_empty;

  // OCTO_SPI
  assign octo_spi = (~ck_ss && tx_armed_s) ? tx_out : 8'hzz;

  // OUTPUT QUEUE
  wire out_r_en;
  wire out_almost_full;
  wire out_almost_empty;
  wire [7:0] out_data;
  wire out_wr_rst_busy;
  wire out_rd_rst_busy;

  reg tx_armed = 1'b0;

  fifo_generator_2 out_queue (
      .din(out_fifo),
      .dout(out_data),
      .almost_full(out_almost_full),
      .full(out_full),
      .almost_empty(out_almost_empty),
      .empty(out_empty),
      .wr_en(out_w_en),
      .rd_en(out_r_en),
      .rst(reset_s),
      .wr_clk(clk),
      .rd_clk(clk),
      .wr_rst_busy(out_wr_rst_busy),
      .rd_rst_busy(out_rd_rst_busy)
  );

  reg [7:0] tx_out;
  assign out_ready = ~out_wr_rst_busy && ~out_rd_rst_busy && ~out_full;

  // CDC synchronizations CK_SCK -> CLK
  always @(posedge ck_sck) begin
    if (reset_s) begin
      tx_event <= 1'b0;
    end else begin
      if (tx_rd_gate) begin
        tx_event <= ~tx_event;
      end

    end
  end

  // TX arming
  wire tx_armed_s = tx_armed_sync[2];

  // TX handshake
  reg [2:0] tx_armed_sync = 3'b000;
  always @(posedge clk) begin
    if (reset) begin
      tx_armed_sync <= 3'b000;
    end else tx_armed_sync <= {tx_armed_sync[1:0], tx_armed};
  end

  // Synchronization for event driven TX (event = ck_SCK pulse)
  wire       tx_event_trigger = tx_event_sync[2] ^ tx_event_sync[1];
  wire       tx_stage_allowed = (state == ST_TX_SEND);
  wire       tx_rd_gate = (~ck_ss) && tx_stage_allowed;

  reg        tx_event = 1'b0;
  reg  [2:0] tx_event_sync = 3'b000;

  always @(posedge clk) begin
    if (reset_s) tx_event_sync <= 3'b000;
    else tx_event_sync <= {tx_event_sync[1:0], tx_event};
  end

  // initialize outgoing event driven buffer, and set conditions for preloading during TX handshake
  reg  tx_first_loaded = 1'b0;
  wire tx_load = (state == ST_TX_SEND) && !out_empty_s && !tx_first_loaded && !out_empty;

  always @(posedge clk) begin
    if (reset_s) begin
      tx_out <= 8'h00;
      tx_first_loaded <= 1'b0;
      tx_armed <= 1'b0;
    end
    if (tx_load) begin
      tx_out <= out_data;
      tx_first_loaded <= 1'b1;
    end else if (tx_event_trigger) begin
      tx_out <= out_data;
    end
    if (state == (ST_TX_SEND) && !ck_ss_s && !out_empty_s) tx_armed <= 1'b1;
    else if (state == (ST_TX_SEND) && ck_ss_s && in_empty) begin
      tx_armed <= 1'b0;
      tx_first_loaded <= 1'b0;
    end
  end

  always_ff @(posedge clk) begin
    if (reset_s) begin
        tx_r_en <= 1'b0;
    end else begin
      tx_r_en <= 1'b0;

      case (state)

        ST_TX_IDLE: begin
          if (~ck_ss_s && ~in_empty) begin
            tx_r_en <= 1'b1;  // TX byte consumed
          end
        end

        ST_TX_SEND: begin
          if (ck_ss_s && ~in_empty) begin
            tx_r_en <= 1'b1;
          end
        end

        default: begin
          // Do nothing...?
        end

      endcase
    end
  end

endmodule
