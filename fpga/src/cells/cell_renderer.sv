// If PixelClockDelay is ever set to 1, this code breaks since reading the pixel takes one cycle, and the next will be used to output the pixel to vga
module cell_renderer (
    input wire active_area,
    input vga_sync_params::x_coordinate_t coord_x,
    input vga_sync_params::y_coordinate_t coord_y,

    output wire [cell_pkg::AddrBits-1:0] read_addr,
    input cell_pkg::cell_word_t          read_data,
    output layer_pkg::layer_px_t pixel
);
  import cell_pkg::*;

  wire in_grid = active_area
      && (coord_x < vga_sync_params::x_coordinate_t'(GridWidth))
      && (coord_y < vga_sync_params::y_coordinate_t'(GridHeight));

  grid_x_t grid_x;
  grid_y_t grid_y;
  assign grid_x = grid_x_t'(coord_x);
  assign grid_y = grid_y_t'(coord_y);

  assign read_addr = in_grid ? get_word_addr(grid_x, grid_y) : '0;

  material_t material;
  assign material = read_data[get_cell_index(grid_x)];

  always_comb begin
    pixel = layer_pkg::TransparentPixel;
    if (in_grid) pixel = layer_pkg::gen_layer_px(props(material).color);
  end
endmodule
