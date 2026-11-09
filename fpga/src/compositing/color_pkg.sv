package color_pkg;

  `define USE_RGB_565_NOT_444

  `ifdef USE_RGB_565_NOT_444
    typedef struct packed {
      logic [5:0] red;
      logic [6:0] green;
      logic [5:0] blue;
    } color_t;
    localparam color_t Black = color_t'(16'h0000);
    `else
    typedef struct packed {
      logic [3:0] red;
      logic [3:0] green;
      logic [3:0] blue;
    } color_t;
    localparam color_t Black = color_t'(12'h000);
  `endif

endpackage
