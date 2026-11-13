module spi_tx #(
  parameter int TurnaroundCycles = 3
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
    error_valid,
    output logic [7:0] error_code
);

  import spi_pck::*;

  // OUTPUT QUEUE
  wire out_r_en;
  wire out_almost_full;
  wire out_almost_empty;
  wire [7:0] out_data;
  wire out_wr_rst_busy;
  wire out_rd_rst_busy;

  reg tx_armed = 1'b0;

  out_buffer out_queue (
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

  reg [7:0] tx_buf [2];
  assign out_ready = ~out_wr_rst_busy && ~out_rd_rst_busy && ~out_full;


  reg rd_window = 1'b0;

  always @(posedge clk) begin
    if(reset_s) rd_window <= 1'b0;
    else rd_window <= (state == ST_TX_IDLE || state == ST_TX_SEND);
  end

  localparam int FirstData = TurnaroundCycles + 1;

  reg [15:0] sck_cnt = 16'd0;
  always @(posedge ck_sck or posedge ck_ss) begin
    if(ck_ss) sck_cnt <= 16'd0;
    else if (sck_cnt != '1) sck_cnt <= sck_cnt + 1'b1;
  end

  reg tx_event = 1'b0;
  always @(negedge ck_sck) begin
    if(rd_window && sck_cnt >= 16'(FirstData + 1)) tx_event <= ~tx_event;
  end


  reg flush = 1'b0;
  always @(posedge clk) begin
    if(reset_s) flush <= 1'b0;
    else flush <= (state == ST_READY || state == ST_FAILED);
  end


  reg drive = 1'b0;
  always @(negedge ck_sck or posedge ck_ss_s) begin
    if(ck_ss_s) drive <= 1'b0;
    else if(rd_window && sck_cnt >= 16'(FirstData)) drive <= 1'b1;
  end

  // Synchronization for event driven TX (event = ck_SCK pulse)
  reg  [2:0] tx_event_sync = 3'b000;
  wire       tx_event_trigger = tx_event_sync[2] ^ tx_event_sync[1];

  always @(posedge clk) begin
    if (reset_s) tx_event_sync <= 3'b000;
    else tx_event_sync <= {tx_event_sync[1:0], tx_event};
  end


  reg [1:0] loaded = 2'd0;
  wire preload = rd_window && (loaded != 2'd2) && !out_empty;
  wire advance = rd_window && (loaded == 2'd2) && tx_event_trigger;

  always @(posedge clk) begin
    if (reset_s) begin
      tx_buf[0] <= 8'h00;
      tx_buf[1] <= 8'h00;
      loaded <= 1'b0;
    end
    if (!rd_window) begin
      loaded <= 1'b0;
    end else if (preload) begin
      tx_buf[tx_event_sync[2] ^ loaded[0]] <= out_data;
      loaded <= loaded + 1'b1;
    end else if (advance) begin
      tx_buf[tx_event_sync[2]] <= out_data;
    end
  end

  always_ff @(posedge clk) begin
    tx_r_en <= 1'b0;
    error_valid <= 1'b0;
    error_code <= 8'h00;
  end

  // MCU READ CONDITIONS
  assign out_r_en = (preload || advance || flush) && ~out_empty;

  // OCTO_SPI
  assign octo_spi = drive ? tx_buf[tx_event] : 8'hzz;

endmodule
