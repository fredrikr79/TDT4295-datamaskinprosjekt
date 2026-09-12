`timescale 1ns / 1ps

module vga_controller (
    input wire clk,
    reset,
    enable,
    output reg h_sync,
    v_sync,
    output reg unsigned [9:0] coord_x,
    coord_y,
    output reg active_area
);
  import vga_sync_params::*;

  // next state regs
  reg h_sync_next, v_sync_next;
  reg unsigned [9:0] coord_x_next, coord_y_next;
  reg active_area_next;
  reg pixel_pulse;

  clock_enable_pulse #(
      .N(PixelClockIncrement)
  ) pixel_pulser (
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .pulse(pixel_pulse)
  );

  always_ff @(posedge clk) begin
    if (pixel_pulse) begin
      if (reset) begin
        h_sync <= 0;
        v_sync <= 0;
        coord_x <= 0;
        coord_y <= 0;
        active_area <= 0;
      end else if(enable) begin
        h_sync <= h_sync_next;
        v_sync <= v_sync_next;
        coord_x <= coord_x_next;
        coord_y <= coord_y_next;
        active_area <= active_area_next;
      end
    end
  end

  always_comb begin
    h_sync_next = 32'(coord_x) >= RightPorch + ActiveWidth &&
        32'(coord_x) < RightPorch + ActiveWidth + HorizontalSync;
    v_sync_next = 32'(coord_y) >= TopPorch + ActiveHeight &&
        32'(coord_y) < TopPorch + ActiveHeight + VerticalSync;

    if (32'(coord_x) == TotalWidth - 1) begin
      coord_x_next = 0;
      if (32'(coord_y) == TotalHeight - 1) coord_y_next = 0;
      else coord_y_next = coord_y + 1;
    end else begin
      coord_x_next = coord_x + 1;
      coord_y_next = coord_y;
    end

    active_area_next = 32'(coord_x) < ActiveWidth && 32'(coord_y) < ActiveHeight;
  end


endmodule
