package sim_pkg;
  localparam int unsigned GridScale = 4;
  localparam int unsigned CellGridWidth = config_pkg::ActiveWidth / GridScale;
  localparam int unsigned CellGridHeight = config_pkg::ActiveHeight / GridScale;

  localparam int GridXBits = $clog2(CellGridWidth);
  localparam int GridYBits = $clog2(CellGridHeight);

  localparam int unsigned MaterialBits = 4;  // support for 16 materials
  localparam int unsigned BitsPerPixel = 8;  // support for 16 materials

  typedef logic [MaterialBits-1:0] material_t;

  // Memory bound processing and wire complexity makes a wider set of cores overkill and infeasible in terms of system resources
  localparam int SimulationCoreCount = 4;
  typedef logic [CellGridWidth * BitsPerPixel - 1 : 0] row_buffer_t;

  // Addressing convention. Everything that touches the grid -- world loader
  // Material IDs
  typedef enum material_t {
    MatAir   = 4'd0,
    MatStone = 4'd1,
    MatSand  = 4'd2,
    MatWater = 4'd3,
    MatLava  = 4'd4,
    MatAcid  = 4'd5,
    MatGOL   = 4'd6,
    MatExplo = 4'd7
  } materials_e;

  // density: heavier materials sink in lighter ones
  // falls: pulled downward by gravity.
  // spreads: flows sideways when blocked underneath (liquid/sand behaviour).
  // Solid: to be recognized as a physical object or a momentary energy / force.
  // A material that neither falls nor spreads is static
  typedef struct packed {
    int                density;
    logic              falls;
    logic              spreads;
    logic              solid;
    logic              displaceable;
    logic [3:0]        blast_resistance;
  } material_props_t;

  localparam int GolRefreshDelay = 'd100;

  // Lookup table for materials
  function automatic material_props_t props(material_t m);
    material_props_t p;
    case (m)
      MatSand: begin
        p.density          = 'd8;
        p.falls            = 1'b1;
        p.spreads          = 1'b0;
        p.solid            = 1'b1;
        p.displaceable     = 1'b1;
        p.blast_resistance = 'd2;
      end
      MatWater: begin
        p.density          = 'd4;
        p.falls            = 1'b1;
        p.spreads          = 1'b1;
        p.solid            = 1'b1;
        p.displaceable     = 1'b1;
        p.blast_resistance = 'd2;
      end
      MatStone: begin
        p.density          = 'd15;
        p.falls            = 1'b0;
        p.spreads          = 1'b0;
        p.solid            = 1'b1;
        p.displaceable     = 1'b0;
        p.blast_resistance = 'd3;
      end
      MatGOL: begin
        p.density          = 15;
        p.falls            = 1'b0;
        p.spreads          = 1'b0;
        p.solid            = 1'b1;
        p.displaceable     = 1'b0;
        p.blast_resistance = 'd3;
      end
      MatExplo: begin
        p.density          = 'd15;
        p.falls            = 1'b0;
        p.spreads          = 1'b0;
        p.solid            = 1'b0;
        p.displaceable     = 1'b0;
        p.blast_resistance = 'd0;
      end
      MatLava: begin
        p.density          = 'd10;
        p.falls            = 1'b1;
        p.spreads          = 1'b1;
        p.solid            = 1'b1;
        p.displaceable     = 1'b1;
        p.blast_resistance = 'd3;
      end
      MatAcid: begin
        p.density          = 'd8;
        p.falls            = 1'b1;
        p.spreads          = 1'b1;
        p.solid            = 1'b1;
        p.displaceable     = 1'b1;
        p.blast_resistance = 'd2;
      end
      default: begin  // MatAir, and every unassigned id
        p.density          = 'd0;
        p.falls            = 1'b0;
        p.spreads          = 1'b0;
        p.solid            = 1'b0;
        p.displaceable     = 1'b1;
        p.blast_resistance = 'd0;
      end
    endcase
    return p;
  endfunction

  // Credits to https://nullprogram.com/blog/2018/07/31/
  function automatic logic [31:0] lowbias32(input logic [31:0] seed);
    logic [31:0] random_value;

    random_value = seed;
    random_value = (random_value ^ (random_value >> 16)) * 32'h7feb_352d;
    random_value = (random_value ^ (random_value >> 15)) * 32'h846c_a68b;
    random_value = random_value ^ (random_value >> 16);

    return random_value[0];
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
    logic [BitsPerPixel-1:0] topLeft;
    logic [BitsPerPixel-1:0] top;
    logic [BitsPerPixel-1:0] topRight;
    logic [BitsPerPixel-1:0] left;
    logic [BitsPerPixel-1:0] center;
    logic [BitsPerPixel-1:0] right;
    logic [BitsPerPixel-1:0] bottomLeft;
    logic [BitsPerPixel-1:0] bottom;
    logic [BitsPerPixel-1:0] bottomRight;
  } neighbourhood_t;

  typedef enum {
    DownNone,
    Down,
    DownLeft,
    DownRight
  } gravity_movement_t;

  typedef struct packed {
    logic topRight;
    logic topLeft;
    logic centerRight;
    logic centerLeft;
  } fluid_movement_t;
  function automatic logic no_movement(fluid_movement_t movement);
    return !movement.topRight && !movement.topLeft && !movement.centerRight && !movement.centerLeft;
  endfunction
endpackage
