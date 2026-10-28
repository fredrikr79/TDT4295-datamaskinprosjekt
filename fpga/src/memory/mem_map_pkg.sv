package mem_map_pkg;
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

  // Cell layer
  localparam int unsigned BytesPerCellWord = cell_pkg::WordBits / 8;  // 4
  localparam int unsigned CellGridBytes = cell_pkg::TotalCellWords * BytesPerCellWord;  // 115,200

  // Framebuffers
  localparam int unsigned FbBytes = GridCells;  // 230,400

  // Color Palette
  localparam int unsigned PaletteEntries = 256;
  localparam int unsigned PaletteEntryBytes = 2;
  localparam int unsigned PaletteBytes = PaletteEntries * PaletteEntryBytes;  // 512

  // HUD sprites
  // TODO


  // Memory pointers //
  localparam int unsigned CellGridMemPointer = 0;
  localparam int unsigned Fb0MemPointer = align_to_next_page(CellGridMemPointer + CellGridBytes);
  localparam int unsigned Fb1MemPointer = align_to_next_page(Fb0MemPointer + FbBytes);
  localparam int unsigned PaletteMemPointer = align_to_next_page(Fb1MemPointer + FbBytes);
  localparam int unsigned SpritesMemPointer = align_to_next_page(PaletteMemPointer + PaletteBytes);

  // Cell layer helper functions //
  function automatic memory_pkg::address_t cell_word_addr(input cell_pkg::grid_x_t x,
                                                          input cell_pkg::grid_y_t y);
    return memory_pkg::address_t
        '(CellGridMemPointer + cell_pkg::get_word_addr(x, y) * BytesPerCellWord);
  endfunction


  // Framebuffer helper functions //
  localparam int unsigned FbXBits = $clog2(GridWidth);  // 10
  localparam int unsigned FbYBits = $clog2(GridHeight);  // 9

  typedef logic unsigned [FbXBits-1:0] fb_x_t;
  typedef logic unsigned [FbYBits-1:0] fb_y_t;

  function automatic memory_pkg::address_t fb_base(input logic buffer_sel);
    return memory_pkg::address_t'(buffer_sel ? Fb1MemPointer : Fb0MemPointer);
  endfunction

  // Start of a scanline. This is the address the line fetcher bursts from
  function automatic memory_pkg::address_t fb_line_addr(input logic buffer_sel, input fb_y_t y);
    return fb_base(buffer_sel) + memory_pkg::address_t'(int'(y) * GridWidth);
  endfunction

  function automatic memory_pkg::address_t fb_byte_addr(input logic buffer_sel, input fb_x_t x,
                                                        input fb_y_t y);
    return fb_line_addr(buffer_sel, y) + memory_pkg::address_t'(int'(x));
  endfunction


  // Palette helper functions //
  localparam int unsigned PaletteIndexBits = $clog2(PaletteEntries);  // 8
  typedef logic unsigned [PaletteIndexBits-1:0] palette_index_t;

  function automatic memory_pkg::address_t palette_entry_addr(input palette_index_t index);
    return memory_pkg::address_t'(PaletteMemPointer + int'(index) * PaletteEntryBytes);
  endfunction

endpackage
