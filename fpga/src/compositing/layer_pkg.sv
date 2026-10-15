package layer_pkg;
  typedef struct packed {
    logic              opaque;
    color_pkg::color_t color;
  } layer_px_t;

  localparam layer_px_t TransparentPixel = layer_px_t'{opaque: 1'b0, color: color_pkg::Black};

  function automatic layer_px_t gen_layer_px(input color_pkg::color_t color);
    return layer_px_t'{opaque: 1'b1, color: color};
  endfunction
endpackage
