`timescale 1ns / 1ps

module vga_game_example (
    input wire clk,
    reset,
    enable,
    up,
    down,
    left,
    right,
    active_area,
    input wire unsigned [9:0] coord_x,
    coord_y,
    output color_pkg::color_t color
);
  localparam int RADIUS = 25;
  localparam color_pkg::color_t CIRCLECOLOR = '{red: 'b1111, green: 'b0000, blue: 'b1111};
  localparam color_pkg::color_t BACKGROUNDCOLOR = '{red: 'b0000, green: 'b1111, blue: 'b0000};
  localparam color_pkg::color_t DEFAULTCOLOR = '{red: 'b0000, green: 'b0000, blue: 'b0000};
  // signal declaration
  reg unsigned [9:0] center_x, center_y;

  // next state regs
  reg unsigned [9:0] center_x_next, center_y_next;
  // sequential logic
  always @(posedge clk) begin
    if (reset) begin
      center_x <= 100;
      center_y <= 100;
    end else if (enable) begin
      center_x <= center_x_next;
      center_y <= center_y_next;
    end
  end
  // moving the circle by one pixel based on inputs
  always_comb begin
    center_x_next = center_x;
    center_y_next = center_y;
    if (up) center_y_next = center_y - 1;
    if (down) center_y_next = center_y + 1;
    if (left) center_x_next = center_x - 1;
    if (right) center_x_next = center_x + 1;
  end
  // check if the current pixel is inside our circle
  reg signed [10:0] dx, dy;
  reg in_circle;
  always_comb begin
    dx = center_x - coord_x;
    dy = center_y - coord_y;
    in_circle = dx * dx + dy * dy <= RADIUS * RADIUS;
    // default value
    color = DEFAULTCOLOR;
    if (active_area) color = BACKGROUNDCOLOR;
    if (active_area && in_circle) color = CIRCLECOLOR;
  end


endmodule
