package cell_pkg;
  localparam int unsigned GridWidth = 640;
  localparam int unsigned GridHeight = 360;

  localparam int unsigned MaterialBits = 4;  // support for 16 materials
  localparam int unsigned CellsPerWord = 8;  // 8x4 = 32 which fits cleanly in registers
  localparam int unsigned WordBits = CellsPerWord * MaterialBits;  // 32: a word contains 8 4-bit cells
  localparam int unsigned WordsPerRow = GridWidth / CellsPerWord;  // 80: a row contains 640 pixels, we contain 8 pixels in one word, 640/8 = 80 which is our screen resolution
  localparam int unsigned TotalWords = WordsPerRow * GridHeight;  // 28,800: To contain our whole screen of 640*360=230400, 230400/8=28800, so we need 28800 words to describe every pixel on screen
  localparam int unsigned AddrBits = $clog2(TotalWords);  // 15 = 32,768, enough to address all of TotalWords

  typedef logic [MaterialBits-1:0] material_t;
  typedef material_t [CellsPerWord-1:0] cell_word_t; // same as logic [7:0][3:0]

  localparam int unsigned CellIndexBits = $clog2(CellsPerWord);  // 3: which cell inside a word
  localparam int unsigned WordXBits = $clog2(WordsPerRow);  // 7: which word inside a row

  localparam int unsigned GridXBits = $clog2(GridWidth);  // 10 needed to represent 640
  localparam int unsigned GridYBits = $clog2(GridHeight);  // 9 needed to represent 360
  typedef logic unsigned [GridXBits-1:0] grid_x_t;
  typedef logic unsigned [GridYBits-1:0] grid_y_t;

  // Addressing convention. Everything that touches the grid -- world loader,
  // simulation step, renderer -- has to agree on this:
  //   addr                 = y * WordsPerRow + x / CellsPerWord
  //   word[get_cell_index(x)]  = the cell at column x
  // Cell index 0 is the LEFT-most of the eight pixels in a word

  function automatic logic [AddrBits-1:0] get_word_addr(input grid_x_t x, input grid_y_t y);
    automatic logic [WordXBits-1:0] word_x = x[GridXBits-1:CellIndexBits];  // x / CellsPerWord
    return AddrBits'(y * WordsPerRow + word_x);
  endfunction

  function automatic logic [CellIndexBits-1:0] get_cell_index(input grid_x_t x);
    return x[CellIndexBits-1:0];
  endfunction

  // Material IDs
  localparam material_t MatAir = 4'd0;
  localparam material_t MatSand = 4'd1;
  localparam material_t MatWater = 4'd2;
  localparam material_t MatStone = 4'd3;

  // density: heavier materials sink in lighter ones
  // falls: pulled downward by gravity.
  // spreads: flows sideways when blocked underneath (liquid/sand behaviour).
  // A material that neither falls nor spreads is static
  typedef struct packed {
    logic [3:0]        density;
    logic              falls;
    logic              spreads;
    color_pkg::color_t color;
  } material_props_t;

  // Lookup Table for materials
  function automatic material_props_t props(material_t m);
    case (m)
      MatSand:
      return '{
          density: 4'd8,
          falls: 1'b1,
          spreads: 1'b0,
          color: '{red: 4'hE, green: 4'hC, blue: 4'h6}
      };
      MatWater:
      return '{
          density: 4'd4,
          falls: 1'b1,
          spreads: 1'b1,
          color: '{red: 4'h2, green: 4'h5, blue: 4'hF}
      };
      MatStone:
      return '{
          density: 4'd15,
          falls: 1'b0,
          spreads: 1'b0,
          color: '{red: 4'h7, green: 4'h7, blue: 4'h7}
      };
      default:  // MatAir, and every unassigned id
      return '{
          density: 4'd0,
          falls: 1'b0,
          spreads: 1'b0,
          color: '{red: 4'h0, green: 4'h0, blue: 4'h1}
      };
    endcase
  endfunction

  // Check for if a cell can displace another cell. Used to put the cell with the most density on the bottom.
  function automatic logic can_displace(material_t mover, material_t target);
    return props(mover).falls && (props(target).density < props(mover).density);
  endfunction
endpackage
