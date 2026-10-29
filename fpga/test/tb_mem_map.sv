// Self-checking tests for mem_map_pkg -- the single authority for "where does
// X live in PSRAM".
//
// The package is pure localparams and functions, so this needs no clock: it
// elaborates the map and asserts the properties the design depends on.
//
// Packages cannot contain an initial block, which is why these checks live here
// rather than inside mem_map_pkg itself. They catch the entire "I moved a base
// and forgot" class, which is otherwise invisible until two regions quietly
// overlap and one of them starts reading the other's data.
//
// Run: make generate-vcd TOP_MODULE=tb_mem_map
module tb_mem_map #(
    parameter string DUMPFILEPATH = ""  // overwritten when compiling with make
) ();
  import mem_map_pkg::*;

  int unsigned errors = 0;

  task automatic check(input string name, input logic condition);
    if (condition !== 1'b1) begin
      $display("FAIL %s", name);
      errors = errors + 1;
    end
  endtask

  task automatic check_eq(input string name, input int unsigned got, input int unsigned want);
    if (got != want) begin
      $display("FAIL %-52s got %0d, want %0d", name, got, want);
      errors = errors + 1;
    end
  endtask

  // The sprite region is still a TODO in mem_map_pkg, pending the decision in
  // the roadmap (hud_pkg::SpriteMemDepth went 64 -> 1024 in the merge from
  // main, which turns a fixed-slot layout into a multi-MiB region). Everything
  // below therefore checks the four regions that ARE defined, and treats
  // SpritesMemPointer as the end of the defined map.
  localparam int unsigned DefinedMapEnd = SpritesMemPointer;

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0);
    end

    $display("mem_map: CellGrid @%0d (%0d B)", CellGridMemPointer, CellGridBytes);
    $display("mem_map: Fb0      @%0d (%0d B)", Fb0MemPointer, FbBytes);
    $display("mem_map: Fb1      @%0d (%0d B)", Fb1MemPointer, FbBytes);
    $display("mem_map: Palette  @%0d (%0d B)", PaletteMemPointer, PaletteBytes);
    $display("mem_map: Sprites  @%0d (size TBD)", SpritesMemPointer);

    // --- Region sizes derive from config_pkg, never hand-typed -------------
    check_eq("GridCells", GridCells, 230400);
    check_eq("CellGridBytes = 4 bits/cell", CellGridBytes, GridCells / 2);
    check_eq("FbBytes = 1 byte/pixel", FbBytes, GridCells);
    check_eq("PaletteBytes", PaletteBytes, PaletteEntries * PaletteEntryBytes);

    // --- Regions are ordered and do not overlap ----------------------------
    // Guaranteed by construction (each base is align_to_next_page of the
    // previous end), so this checks the chain was written correctly.
    check("Fb0 starts after CellGrid", Fb0MemPointer >= CellGridMemPointer + CellGridBytes);
    check("Fb1 starts after Fb0", Fb1MemPointer >= Fb0MemPointer + FbBytes);
    check("Palette starts after Fb1", PaletteMemPointer >= Fb1MemPointer + FbBytes);
    check("Sprites start after Palette", SpritesMemPointer >= PaletteMemPointer + PaletteBytes);

    // --- Every base is page aligned ----------------------------------------
    // PageBytes matches the 1 KiB array row. Alignment is an allocation
    // convenience, not a burst constraint -- linear bursts cross rows fine.
    check("CellGrid page aligned", (CellGridMemPointer % PageBytes) == 0);
    check("Fb0 page aligned", (Fb0MemPointer % PageBytes) == 0);
    check("Fb1 page aligned", (Fb1MemPointer % PageBytes) == 0);
    check("Palette page aligned", (PaletteMemPointer % PageBytes) == 0);
    check("Sprites page aligned", (SpritesMemPointer % PageBytes) == 0);

    // --- The map fits inside ONE die ---------------------------------------
    // Stronger than "no region crosses the boundary", and the property that
    // actually holds today. "Linear Burst across die boundary is not
    // supported" -- a straddling burst is silent corruption, not a clean
    // failure, so this must stay true as the sprite region gets sized.
    check("defined map fits in die 0", DefinedMapEnd <= DieBytes);
    check("defined map fits the device", DefinedMapEnd <= DeviceBytes);

    // --- Cell layer helpers -------------------------------------------------
    // cell_pkg packs 8 cells per 32-bit word, so neighbouring columns inside
    // one word share an address. Getting this wrong shears the world by a
    // sub-word offset, which looks like noise rather than an off-by-one.
    check_eq("cell_word_addr(0,0)", cell_word_addr(0, 0), CellGridMemPointer);
    check_eq("cell_word_addr(1,0) shares a word with (0,0)", cell_word_addr(1, 0),
             cell_word_addr(0, 0));
    check_eq("cell_word_addr(7,0) shares a word with (0,0)", cell_word_addr(7, 0),
             cell_word_addr(0, 0));
    check_eq("cell_word_addr(8,0) is the next word", cell_word_addr(8, 0),
             cell_word_addr(0, 0) + BytesPerCellWord);
    check_eq("cell_word_addr(0,1) is one row on", cell_word_addr(0, 1),
             CellGridMemPointer + (GridWidth / cell_pkg::CellsPerWord) * BytesPerCellWord);
    check("last cell stays inside CellGrid",
          cell_word_addr(cell_pkg::grid_x_t'(GridWidth - 1),
                         cell_pkg::grid_y_t'(GridHeight - 1)) + BytesPerCellWord
          <= CellGridMemPointer + CellGridBytes);

    // --- Framebuffer helpers ------------------------------------------------
    check_eq("fb_base(0)", fb_base(1'b0), Fb0MemPointer);
    check_eq("fb_base(1)", fb_base(1'b1), Fb1MemPointer);
    check_eq("fb_line_addr(0, 0)", fb_line_addr(1'b0, 0), Fb0MemPointer);
    check_eq("fb_line_addr stride is one line", fb_line_addr(1'b0, 1),
             Fb0MemPointer + GridWidth);
    check_eq("fb_byte_addr(0, 0, 0)", fb_byte_addr(1'b0, 0, 0), Fb0MemPointer);

    // The last pixel of each buffer must land on that buffer's last byte --
    // one past it would scribble on the next region.
    check_eq("fb_byte_addr last pixel of Fb0",
             fb_byte_addr(1'b0, fb_x_t'(GridWidth - 1), fb_y_t'(GridHeight - 1)),
             Fb0MemPointer + FbBytes - 1);
    check_eq("fb_byte_addr last pixel of Fb1",
             fb_byte_addr(1'b1, fb_x_t'(GridWidth - 1), fb_y_t'(GridHeight - 1)),
             Fb1MemPointer + FbBytes - 1);

    // A whole scanline fetched from the last line must stay inside the buffer.
    check("last line fetch stays inside Fb1",
          fb_line_addr(1'b1, fb_y_t'(GridHeight - 1)) + GridWidth
          <= Fb1MemPointer + FbBytes);

    // --- Palette helpers ----------------------------------------------------
    check_eq("palette_entry_addr(0)", palette_entry_addr(0), PaletteMemPointer);
    check_eq("palette_entry_addr(1)", palette_entry_addr(1),
             PaletteMemPointer + PaletteEntryBytes);
    check_eq("palette_entry_addr(255) is the last entry", palette_entry_addr(255),
             PaletteMemPointer + PaletteBytes - PaletteEntryBytes);

    if (errors != 0) $fatal(1, "tb_mem_map: %0d check(s) failed", errors);
    $display("tb_mem_map: all checks passed");
    $finish;
  end
endmodule
