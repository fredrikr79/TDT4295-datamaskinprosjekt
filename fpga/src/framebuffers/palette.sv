module palette (
    input palette_pkg::palette_index_t index,
    output color_pkg::color_t color
);

  color_pkg::color_t entries[palette_pkg::PaletteEntries];

  // 0-15    cell_pkg material colours
  // 16-255  a 3-3-2 RGB ramp widened to 4-4-4.
  initial begin
    for (int unsigned i = 0; i < palette_pkg::PaletteEntries; i++) begin
      if (i < 16) begin
        entries[i] = cell_pkg::material_color(cell_pkg::material_t'(i));
      end else begin
        entries[i] = color_pkg::color_t'({
          i[7:5], i[7],  // abca
          i[4:2], i[4],  // abca
          i[1:0], i[1:0]  // abab
        });
      end
    end
  end

  assign color = entries[index];
endmodule
