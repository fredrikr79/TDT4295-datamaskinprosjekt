package mem_map_pkg;
  // config_pkg sizes these as reg [15:0], so widen explicitly -- --Wall treats
  // the implicit extension as a WIDTHEXPAND warning, and warnings are fatal.
  localparam int unsigned GridWidth = int'(config_pkg::ActiveWidth);  // 640
  localparam int unsigned GridHeight = int'(config_pkg::ActiveHeight);  // 360
  localparam int unsigned GridCells = GridWidth * GridHeight;  // 230,400

  // Device: Infineon S70KL1283, 128 Mbit HyperRAM, xSPI (Octal) interface.
  localparam int unsigned PageBytes = 1024;

  // 128 Mbit SRAM, two stacked 64 Mbit dice = 8 MiB each.
  // Linear Burst across die boundary is not supported.
  localparam int unsigned DieBytes = 8 * 1024 * 1024;  // 8 MiB per die
  localparam int unsigned DeviceBytes = 2 * DieBytes;  // Max capacity of SRAM

  // Helps us create memory-pointers that point to the start of pages. Just for convenience.
  function automatic int unsigned align_to_next_page(input int unsigned value);
    return ((value + PageBytes - 1) / PageBytes) * PageBytes;
  endfunction

  // Region sizes //
  // Gol layer
  localparam int unsigned GolCellsPerByte = 8;
  localparam int unsigned GolGridBytes = gol_pkg::BitsPerFrame / GolCellsPerByte;  // 28,800

  localparam int unsigned GolBitIndexBits = $clog2(GolCellsPerByte);  // 3
  typedef logic unsigned [GolBitIndexBits-1:0] gol_bit_index_t;

  // Cell layer
  localparam int unsigned BytesPerCellWord = cell_pkg::WordBits / 8;  // 4
  localparam int unsigned CellGridBytes = cell_pkg::TotalCellWords * BytesPerCellWord;  // 115,200

  // Framebuffers
  localparam int unsigned FbBytes = GridCells;  // 230,400

  // Memory pointers //
  localparam int unsigned GolGridMemPointer = 0;
  localparam int unsigned CellGridMemPointer = align_to_next_page(GolGridMemPointer + GolGridBytes);
  localparam int unsigned Fb0MemPointer = align_to_next_page(CellGridMemPointer + CellGridBytes);
  localparam int unsigned Fb1MemPointer = align_to_next_page(Fb0MemPointer + FbBytes);
  localparam int unsigned SpritesMemPointer = align_to_next_page(Fb1MemPointer + FbBytes);


  // Gol helper functions //
  function automatic memory_pkg::address_t gol_byte_addr(input gol_pkg::grid_x_t x,
                                                         input gol_pkg::grid_y_t y);
    return gol_byte_addr_at_index(gol_pkg::cell_index(x, y));
  endfunction

  // Used by gol_byte_addr
  function automatic memory_pkg::address_t gol_byte_addr_at_index(
      input gol_pkg::cell_index_t cell_index
    );
    return memory_pkg::address_t'(GolGridMemPointer + (int'(cell_index) / GolCellsPerByte));
  endfunction

  function automatic gol_bit_index_t gol_bit_index(input gol_pkg::grid_x_t x,
                                                   input gol_pkg::grid_y_t y);
    // 7 - (index % 8) gets us leftmost cell, which is our layout, MSB is first cell.
    return gol_bit_index_t'(GolCellsPerByte - 1 - (int'(gol_pkg::cell_index(x, y)) % GolCellsPerByte));
  endfunction


  // Cells helper functions //
  function automatic memory_pkg::address_t cell_word_addr(input cell_pkg::grid_x_t x,
                                                          input cell_pkg::grid_y_t y);
    return memory_pkg::address_t
        '(CellGridMemPointer + cell_pkg::get_word_addr(x, y) * BytesPerCellWord);
  endfunction

  // Framebuffer helper functions //
  localparam int unsigned FbXBits = $clog2(GridWidth);   // 10
  localparam int unsigned FbYBits = $clog2(GridHeight);  // 9

  typedef logic unsigned [FbXBits-1:0] fb_x_t;
  typedef logic unsigned [FbYBits-1:0] fb_y_t;

  function automatic memory_pkg::address_t fb_base(input logic buffer_sel);
    return memory_pkg::address_t'(buffer_sel ? Fb1MemPointer : Fb0MemPointer);
  endfunction

  function automatic memory_pkg::address_t fb_byte_addr(input logic buffer_sel, input fb_x_t x,
                                                        input fb_y_t y);
    return fb_base(buffer_sel) + memory_pkg::address_t'(int'(y) * GridWidth + int'(x));
  endfunction
endpackage
