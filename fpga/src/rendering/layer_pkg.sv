package layer_pkg;
  typedef struct packed {
    logic              opaque;
    color_pkg::color_t color;
  } layer_px_t;

  localparam layer_px_t TransparentPixel = '{opaque: 1'b0, color: '{red: 4'h0, green: 4'h0, blue: 4'h0}};

  function automatic layer_px_t gen_layer_px(input color_pkg::color_t color);
    return layer_px_t'{opaque: 1'b1, color: color};
  endfunction
endpackage
