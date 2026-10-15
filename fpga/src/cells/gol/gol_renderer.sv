module gol_renderer (
    input wire active_area,
    input wire alive,
    output layer_pkg::layer_px_t pixel
);
  localparam color_pkg::color_t AliveColor = '{red: 4'hF, green: 4'hF, blue: 4'hF};

  always_comb begin
    pixel = layer_pkg::TransparentPixel;
    if (active_area && alive) begin
      pixel = layer_pkg::gen_layer_px(AliveColor);
    end
  end
endmodule
