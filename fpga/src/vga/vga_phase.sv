module vga_phase #(
    // How many lines of blanking that are left before we drive near_frame_end high.
    parameter int unsigned GuardLines = 1
) (
    input logic clk,
    input logic reset,
    input vga_sync_params::x_coordinate_t coord_x,
    input vga_sync_params::y_coordinate_t coord_y,
    input logic pixel_pulse,
    output logic v_blank,
    output logic h_blank,
    output logic near_frame_end,
    output logic frame_start
);
  import vga_sync_params::*;
  import config_pkg::*;

  assign h_blank = 32'(coord_x) >= 32'(ActiveWidth);
  assign v_blank = 32'(coord_y) >= 32'(ActiveHeight);

  assign near_frame_end = 32'(coord_y) >= TotalHeight - GuardLines;

  logic line_end;
  logic last_pixel_in_line, last_line_in_blanking;
  assign last_pixel_in_line = 32'(coord_x) == TotalWidth - 1;
  assign last_line_in_blanking = 32'(coord_y) == TotalHeight - 1;
  assign line_end = pixel_pulse && last_pixel_in_line;

  always_ff @(posedge clk) begin
    if (reset) frame_start <= 1'b0;
    else frame_start <= line_end && last_line_in_blanking;
  end

endmodule
