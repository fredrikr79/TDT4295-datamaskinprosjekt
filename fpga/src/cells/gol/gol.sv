module gol (
    input wire clk,
    input wire reset,

    input logic active_area,
    pixel_pulse,

    output layer_pkg::layer_px_t pixel
);
  wire alive;

  gol_simulator simulator (
      .clk  (clk),
      .reset(reset),
      .en   (pixel_pulse && active_area),
      .alive(alive)
  );

  gol_renderer renderer (
      .active_area(active_area),
      .alive      (alive),
      .pixel      (pixel)
  );
endmodule
