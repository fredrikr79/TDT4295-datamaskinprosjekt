package mem_map_pkg;
  localparam int unsigned GridWidth = sim_pkg::CellGridWidth;  // 160
  localparam int unsigned GridHeight = sim_pkg::CellGridHeight;  // 90
  localparam int unsigned GridCells = GridWidth * GridHeight;  // 230,400

  localparam int unsigned ScreenWidth = config_pkg::ActiveWidth;  // 640
  localparam int unsigned ScreenHeight = config_pkg::ActiveHeight;  // 360
  localparam int unsigned ScreenCells = ScreenWidth * ScreenHeight;  // 360

  // Device: Infineon S70KL1283, 128 Mbit HyperRAM, xSPI (Octal) interface.
  localparam int unsigned PageBytes = 1024;

  // 128 Mbit SRAM, two stacked 64 Mbit dice = 8 MiB each.
  // Linear Burst across die boundary is not supported.
  localparam int unsigned DieBytes = 8 * 1024 * 1024;  // 8 MiB per die
  localparam int unsigned DeviceBytes = 2 * DieBytes;  // Max capacity of SRAM

  // Framebuffers
  localparam int unsigned FbBytes = ScreenCells;  // 230,400

  // HUD sprites
  // TODO

  // Memory pointers //
  localparam int unsigned CellGridMemPointer = 0;
  localparam int unsigned Fb0MemPointer = 0;
  localparam int unsigned Fb1MemPointer = Fb0MemPointer + FbBytes;
  localparam int unsigned SpritesMemPointer = Fb1MemPointer + FbBytes;

  // Framebuffer helper functions //
  localparam int unsigned FbXBits = $clog2(ScreenWidth);
  localparam int unsigned FbYBits = $clog2(ScreenHeight);

  typedef logic unsigned [FbXBits-1:0] fb_x_t;
  typedef logic unsigned [FbYBits-1:0] fb_y_t;

  function automatic memory_pkg::address_t fb_base(input logic buffer_sel);
    return memory_pkg::address_t'(buffer_sel ? Fb1MemPointer : Fb0MemPointer);
  endfunction

  // Start of a scanline. This is the address the line fetcher bursts from
  function automatic memory_pkg::address_t fb_line_addr(input logic buffer_sel, input fb_y_t y);
    return fb_base(buffer_sel) + memory_pkg::address_t'(int'(y) * ScreenWidth);
  endfunction

  function automatic memory_pkg::address_t fb_byte_addr(input logic buffer_sel, input fb_x_t x,
                                                        input fb_y_t y);
    return fb_line_addr(buffer_sel, y) + memory_pkg::address_t'(int'(x));
  endfunction

  parameter int unsigned GridMemLocationStart = 32'd0;
  parameter int unsigned BytesPerGridPixel = 1;

  // Memory transaction states
  typedef enum bit [3:0] {
      MEM_START,
      MEM_GET_X_TOP,
      MEM_GET_X_BOTTOM,
      MEM_GET_Y_TOP,
      MEM_GET_Y_BOTTOM,
      MEM_GET_MSG_LENGTH_TOP,
      MEM_GET_MSG_LENGTH_BOTTOM,
      MEM_GET_MEM_ADDR,
      MEM_INIT_REQ,
      MEM_DATA,
      MEM_RETURN_TOP,
      MEM_RETURN_BOTTOM,
      MEM_DONE
  } mem_states_e;

  function static logic [31:0] GridPosToMemAddr (input logic [15:0] x, input logic [15:0] y);
      return GridMemLocationStart + y * sim_pkg::CellGridWidth + 32'(x);
  endfunction

endpackage
