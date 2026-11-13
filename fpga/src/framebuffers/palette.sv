module palette (
    input palette_pkg::palette_index_t index,
    output color_pkg::color_t color
);
  localparam int unsigned MaterialCount = 8;  // sim_pkg::materials_e members
  localparam int unsigned CellIndexLimit = MaterialCount * 16;  // 128

  color_pkg::color_t entries[palette_pkg::PaletteEntries];

  initial begin
    for (int unsigned i = 0; i < palette_pkg::PaletteEntries; i++) begin
      if (i < CellIndexLimit) begin
        entries[i] = sim_pkg::material_color(sim_pkg::material_t'(i[7:4]));
      end else begin
        entries[i] = color_pkg::color_t'({
          i[6:4], i[6],  // abca
          i[3:2], i[3:2],  // abab
          i[1:0], i[1:0]  // abab
        });
      end
    end
  end

  assign color = entries[index];
endmodule
