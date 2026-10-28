package sim_pkg;
  localparam int unsigned CellGridWidth = config_pkg::ActiveWidth;
  localparam int unsigned CellGridHeight = config_pkg::ActiveHeight;

  localparam int unsigned MaterialBits = 4;  // support for 16 materials
  localparam int unsigned BitsPerPixel = 8;  // support for 16 materials
  localparam int unsigned CellsPerWord = 4;  // 8x4 = 32 which fits cleanly in registers
  localparam int unsigned WordBits = CellsPerWord * BitsPerPixel;  // 32: a word contains 8 4-bit cells
  localparam int unsigned WordsPerRow = CellGridWidth / CellsPerWord;  // 80: a row contains 640 pixels, we contain 8 pixels in one word, 640/8 = 80 which is our screen resolution
  localparam int unsigned TotalWords = WordsPerRow * CellGridHeight;  // 28,800: To contain our whole screen of 640*360=230400, 230400/8=28800, so we need 28800 words to describe every pixel on screen
  localparam int unsigned AddrBits = $clog2(TotalWords);  // 15 = 32,768, enough to address all of TotalWords

  typedef logic [MaterialBits-1:0] material_t;
  typedef material_t [CellsPerWord-1:0] cell_word_t; // same as logic [7:0][3:0]

  localparam int unsigned CellIndexBits = $clog2(CellsPerWord);  // 3: which cell inside a word
  localparam int unsigned WordXBits = $clog2(WordsPerRow);  // 7: which word inside a row

  localparam int unsigned GridXBits = $clog2(CellGridWidth);  // 10 needed to represent 640
  localparam int unsigned GridYBits = $clog2(CellGridHeight);  // 9 needed to represent 360
  typedef logic unsigned [GridXBits-1:0] grid_x_t;
  typedef logic unsigned [GridYBits-1:0] grid_y_t;

  localparam int SimulationCoreCoun = 4; // Memory bound processing and wire complexity makes a wider set of cores overkill and infeasible in terms of system resources 

  // Addressing convention. Everything that touches the grid -- world loader,
  // simulation step, renderer -- has to agree on this:
  //   addr                 = y * WordsPerRow + x / CellsPerWord
  //   word[get_cell_index(x)]  = the cell at column x
  // Cell index 0 is the LEFT-most of the eight pixels in a word

  function automatic logic [AddrBits-1:0] get_word_addr(input grid_x_t x, input grid_y_t y);
    automatic logic [WordXBits-1:0] word_x;
    word_x = x[GridXBits-1:CellIndexBits];  // x / CellsPerWord
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
  localparam material_t MatPortal = 4'd4;
  localparam material_t MatLava = 4'd5;
  localparam material_t MatAcid = 4'd6;

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

  // Color lookup table
  function automatic color_pkg::color_t material_color(input material_t m);
    logic [11:0] rgb;
    case (m)
      MatSand:   rgb = 12'hEC6;  // sandy yellow
      MatWater:  rgb = 12'h25F;  // blue
      MatStone:  rgb = 12'h777;  // grey
      MatPortal: rgb = 12'h519;  // purple
      MatLava:   rgb = 12'hF60;  // orange
      MatAcid:   rgb = 12'h8F0;  // acid green
      default:   rgb = 12'h001;  // MatAir, and every unassigned id
    endcase
    return color_pkg::color_t'(rgb);
  endfunction

  // Lookup table for materials
  function automatic material_props_t props(material_t m);
    material_props_t p;
    p.color = material_color(m);
    case (m)
      MatSand: begin
        p.density = 4'd8;
        p.falls   = 1'b1;
        p.spreads = 1'b0;
      end
      MatWater: begin
        p.density = 4'd4;
        p.falls   = 1'b1;
        p.spreads = 1'b1;
      end
      MatStone: begin
        p.density = 4'd15;
        p.falls   = 1'b0;
        p.spreads = 1'b0;
      end
      MatPortal: begin
        p.density = 4'd15;
        p.falls   = 1'b0;
        p.spreads = 1'b0;
      end
      MatLava: begin
        p.density = 4'd10;
        p.falls   = 1'b1;
        p.spreads = 1'b1;
      end
      MatAcid: begin
        p.density = 4'd8;
        p.falls   = 1'b1;
        p.spreads = 1'b1;
      end
      default: begin  // MatAir, and every unassigned id
        p.density = 4'd0;
        p.falls   = 1'b0;
        p.spreads = 1'b0;
      end
    endcase
    return p;
  endfunction

  // Check for if a cell can displace another cell. Used to put the cell with the most density on the bottom.
  function automatic logic can_displace(material_t mover, material_t target);
    material_props_t mover_props;
    material_props_t target_props;
    mover_props  = props(mover);
    target_props = props(target);
    return mover_props.falls && (target_props.density < mover_props.density);
  endfunction

  typedef struct packed {
    logic [MaterialBits] topLeft;
    logic [MaterialBits] top;
    logic [MaterialBits] topRight;
    logic [MaterialBits] left;
    logic [MaterialBits] center;
    logic [MaterialBits] right;
    logic [MaterialBits] bottomLeft;
    logic [MaterialBits] bottom;
    logic [MaterialBits] bottomRight;
  } pixel_neighbourhood_t;


endpackage
