// Milestone 5 gate: the framebuffer reaches the screen, and starvation is a
// test failure rather than something noticed by eye three milestones later.
//
// Scan-out is the only path in the design with NO BACK-PRESSURE. If the line
// fetcher is late the beam reads stale RAM and the result is a corrupt line --
// there is no stall, no error flag anyone acts on, just a wrong picture. So the
// two things checked here are:
//
//   1. fb_underrun never asserts during active area, over two full frames,
//      WITH A COMPETING CLIENT SATURATING THE BUS. Testing an idle bus would
//      prove nothing; the whole question is whether top priority plus one line
//      of buffering is enough when something else wants the memory.
//   2. The pixels are actually the framebuffer's. An underrun-free run that
//      displays the wrong bytes is not a pass.
//
// The budget being verified (architecture doc section 6): ~850 clocks of fetch
// work plus at most ~850 of arbitration blocking, against a 3,200-clock line.
//
// Run: make generate-vcd TOP_MODULE=tb_fb_scanout
module tb_fb_scanout #(
    // Declared so `make generate-vcd` can pass -G, but deliberately UNUSED:
    // this runs for two full frames and Verilator ignores $dumpoff, so dumping
    // would produce a multi-hundred-megabyte trace. Narrow the run and add
    // $dumpvars by hand if a waveform is actually needed.
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned Width = mem_map_pkg::GridWidth;
  localparam int unsigned Height = mem_map_pkg::GridHeight;

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1, enable = 0;

  // --- raster -------------------------------------------------------------
  // The real vga block, so the timing under test is the timing that ships.
  logic active_area, pixel_pulse, v_blank, h_blank, frame_start, near_frame_end, line_end;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t vga_color;
  wire [3:0] vga_r, vga_g, vga_b;
  wire h_sync, v_sync;

  vga raster (
      .clk(clk), .reset(reset), .enable(enable), .color(vga_color),
      .vga_r(vga_r), .vga_g(vga_g), .vga_b(vga_b),
      .coord_x(coord_x), .coord_y(coord_y),
      .active_area(active_area), .pixel_pulse(pixel_pulse),
      .h_sync(h_sync), .v_sync(v_sync),
      .v_blank(v_blank), .h_blank(h_blank),
      .frame_start(frame_start), .near_frame_end(near_frame_end), .line_end(line_end)
  );

  // --- memory -------------------------------------------------------------
  localparam int unsigned N = memory_pkg::NumMemClients;
  localparam int unsigned Scan = memory_pkg::MemClientScanoutID;
  localparam int unsigned Hog = memory_pkg::MemClientSimID;  // lowest priority

  logic [N-1:0] client_toggle;
  memory_pkg::transaction_e client_transaction[N];
  memory_pkg::address_t client_address[N];
  memory_pkg::message_length_t client_length[N];
  logic [7:0] client_write_data[N];
  logic [N-1:0] client_allow = '1;
  logic [N-1:0] client_busy, client_data_on_cable, client_grant;
  logic [7:0] mem_read_data;

  wire out_clk, out_clk_n, cs_n, reset_n, rwds;
  wire [7:0] dq;

  memory memory_subsystem (
      .clk(clk), .reset(reset), .enable(enable),
      .client_toggle, .client_next_transaction(client_transaction),
      .client_address, .client_length, .client_write_data, .client_allow,
      .client_busy, .client_data_on_cable, .client_grant, .read_data(mem_read_data),
      .out_clk, .out_clk_n, .cs_n, .reset_n, .dq, .rwds
  );

  psram_model psram (
      .cs_n(cs_n), .ck(out_clk), .reset_n(reset_n), .dq(dq), .rwds(rwds)
  );

  // --- the device under test ----------------------------------------------
  layer_pkg::layer_px_t fb_pixel;
  logic fb_underrun;
  logic fb_mem_toggle;
  memory_pkg::transaction_e fb_mem_transaction;
  memory_pkg::address_t fb_mem_address;
  memory_pkg::message_length_t fb_mem_length;

  fb_scanout dut (
      .clk(clk), .reset(reset),
      .active_area(active_area), .coord_x(coord_x), .coord_y(coord_y),
      .line_end(line_end), .near_frame_end(near_frame_end), .fb_sel(1'b0),
      .mem_toggle(fb_mem_toggle), .mem_transaction(fb_mem_transaction),
      .mem_address(fb_mem_address), .mem_length(fb_mem_length),
      .mem_busy(client_busy[Scan]), .mem_data_on_cable(client_data_on_cable[Scan]),
      .mem_read_data(mem_read_data),
      .pixel(fb_pixel), .underrun(fb_underrun)
  );

  assign vga_color = fb_pixel.color;

  // --- a client that never stops asking -----------------------------------
  // Sits at the BOTTOM of the priority order and issues back-to-back
  // maximum-length reads. It cannot preempt scan-out, but every burst it starts
  // is a burst scan-out may have to wait out -- which is exactly the blocking
  // the deadline budget allows for.
  logic hog_toggle = 0;
  always_ff @(posedge clk) begin
    if (reset) hog_toggle <= 1'b0;
    else if (enable && !client_busy[Hog] && memory_subsystem.initializer_finished)
      hog_toggle <= ~hog_toggle;
  end

  always_comb begin
    client_toggle = '0;
    for (int unsigned i = 0; i < N; i++) begin
      client_transaction[i] = memory_pkg::NONE;
      client_address[i] = '0;
      client_length[i] = '0;
      client_write_data[i] = '0;
    end

    client_toggle[Scan]      = fb_mem_toggle;
    client_transaction[Scan] = fb_mem_transaction;
    client_address[Scan]     = fb_mem_address;
    client_length[Scan]      = fb_mem_length;

    client_toggle[Hog]      = hog_toggle;
    client_transaction[Hog] = memory_pkg::READ;
    client_address[Hog]     = memory_pkg::address_t'(mem_map_pkg::CellGridMemPointer);
    client_length[Hog]      = memory_pkg::message_length_t'(memory_pkg::MaxMessageLength);
  end

  // --- expected image -----------------------------------------------------
  // Varies along both axes so a line fetched from the wrong address, or an
  // off-by-one in the double buffer swap, shows up as a mismatch rather than
  // looking plausible.
  function automatic logic [7:0] pattern(input int unsigned x, input int unsigned y);
    return 8'((x * 3 + y * 7) & 32'hFF);
  endfunction

  // Separate counters per monitor. Two always blocks incrementing one variable
  // is a race, and a flaky failure count is worse than none.
  int unsigned underrun_errors = 0;
  int unsigned pixel_errors = 0;
  int unsigned checked = 0;
  int unsigned frames = 0;
  logic checking = 0;

  // --- underrun monitor ---------------------------------------------------
  always @(posedge clk) begin
    if (checking && fb_underrun) begin
      $display("FAIL underrun at x=%0d y=%0d", coord_x, coord_y);
      underrun_errors = underrun_errors + 1;
    end
  end

  // --- pixel monitor ------------------------------------------------------
  // fb_scanout presents the pixel one cycle after it indexes the line buffer,
  // so compare against the previous coordinate.
  vga_sync_params::x_coordinate_t x_q;
  vga_sync_params::y_coordinate_t y_q;
  logic active_q;
  always_ff @(posedge clk) begin
    x_q <= coord_x;
    y_q <= coord_y;
    active_q <= active_area && (32'(coord_x) < Width);
  end

  color_pkg::color_t expect_color;
  // The colour table used to be a ROM inside the dut; it is now a function in
  // color_pkg. What this still pins down is the byte -- that the pixel reaching
  // the beam is the one the pattern put at that coordinate.
  always_comb expect_color = color_pkg::color_lookup(pattern(32'(x_q), 32'(y_q)));

  always @(posedge clk) begin
    if (checking && active_q && (32'(y_q) < Height)) begin
      checked = checked + 1;
      if (fb_pixel.color !== expect_color) begin
        if (pixel_errors < 10)
          $display("FAIL pixel (%0d,%0d): got %h want %h", x_q, y_q, fb_pixel.color, expect_color);
        pixel_errors = pixel_errors + 1;
      end
    end
  end

  always @(posedge clk) if (frame_start) frames = frames + 1;

  // --- deadline measurement ----------------------------------------------
  // Architecture doc section 6 budgets ~850 clocks of fetch work plus at most
  // ~850 of arbitration blocking, against a 3,200-clock line. Measuring it
  // turns that prediction into a number: if the worst case is materially above
  // 1,700 something is granting out of priority order.
  int unsigned fetch_clocks = 0, fetch_worst = 0;
  logic fetch_timing = 0;
  always @(posedge clk) begin
    if (line_end) begin
      fetch_clocks <= 0;
      fetch_timing <= 1'b1;
    end else if (fetch_timing) begin
      fetch_clocks <= fetch_clocks + 1;
      if (32'(dut.fetcher.bytes_received) >= 32'(Width)) begin
        fetch_timing <= 1'b0;
        if (checking && fetch_clocks + 1 > fetch_worst) fetch_worst <= fetch_clocks + 1;
      end
    end
  end

  initial begin
    // Preload the framebuffer directly into the model's backing store. A smoke
    // client that wrote it over the bus would be testing the write path, not
    // the display path, and would contend for the very bandwidth under test.
    for (int unsigned y = 0; y < Height; y++)
      for (int unsigned x = 0; x < Width; x++)
        psram.mem[mem_map_pkg::Fb0MemPointer + y * Width + x] = pattern(x, y);

    repeat (4) @(posedge clk);
    reset  = 0;
    enable = 1;

    wait (memory_subsystem.initializer_finished);
    $display("tb_fb_scanout: initializer done, waiting for a clean frame boundary");

    // Skip the partial frame we started in, plus one full frame of warm-up --
    // the line buffer is legitimately empty out of reset.
    wait (frames == 2);
    @(posedge clk);
    checking = 1;
    $display("tb_fb_scanout: checking from frame 2, hog client saturating the bus");

    wait (frames == 4);
    checking = 0;

    if (checked < Width * Height)
      $fatal(1, "only %0d pixels checked, expected at least %0d", checked, Width * Height);
    if (underrun_errors != 0 || pixel_errors != 0)
      $fatal(1, "tb_fb_scanout: %0d underrun(s), %0d wrong pixel(s)",
             underrun_errors, pixel_errors);

    $display("tb_fb_scanout: %0d pixels over 2 frames, no underrun, image correct", checked);
    $display("tb_fb_scanout: worst-case line fetch %0d clocks (budget 1700 of 3200 per line)",
             fetch_worst);
    if (fetch_worst > 3200)
      $fatal(1, "line fetch took %0d clocks, past the %0d-clock line period", fetch_worst, 3200);
    $finish;
  end

  initial begin
    #20000000;
    $fatal(1, "tb_fb_scanout: timeout (frames=%0d checked=%0d)", frames, checked);
  end
endmodule
