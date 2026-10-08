module compositor #(
    parameter int unsigned NumLayers = 2
) (
    input wire active_area,
    input layer_pkg::layer_px_t layers[NumLayers],
    output color_pkg::color_t color
);
  localparam color_pkg::color_t BlankColor = '{red: 4'h0, green: 4'h0, blue: 4'h0};

  always_comb begin
    color = BlankColor;  // VGA requires black outside the active area
    if (active_area) begin
      for (int unsigned i = 0; i < NumLayers; i++) begin
        if (layers[i].opaque) begin
          color = layers[i].color;
        end
      end
    end
  end
endmodule
