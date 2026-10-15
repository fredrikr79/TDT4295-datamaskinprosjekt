package color_pkg;
  typedef struct packed {
    logic [3:0] red;
    logic [3:0] green;
    logic [3:0] blue;
  } color_t;

  localparam color_t Black = color_t'(12'h000);
endpackage
