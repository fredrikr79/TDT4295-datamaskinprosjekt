module tb_sim #(
    parameter int NUM_GENERATIONS = 100,
    parameter string DUMPDIR = "",
    parameter string DUMPFILENAME = "", // these parameters can be overwritten when compiling with make
    parameter string GRID_FILE = ""
);
  import sim_pkg::*;

  localparam int W = CellGridWidth;
  localparam int H = CellGridHeight;
  localparam int MAX_ROW_WAIT_CYCLES  = 100_000;
  localparam int MAX_PAIR_WAIT_CYCLES = 100_000;

  logic clk = 1'b0;
  logic pulse_new_row = 1'b0;
  logic sim_running = 1'b0;
  logic pulse_row_done;
  logic pulse_transaction_pair_done;

  row_buffer_t buffers [3];
  row_buffer_t res_buffer [2];

  // Full 8-bit cell values: material ID [7:4], counter/age [3:0].
  logic [7:0] current_grid [0:H-1][0:W-1];
  logic [7:0] next_grid    [0:H-1][0:W-1];
  logic [7:0] input_bytes  [0:W*H-1];

  integer frame_number = 0;
  integer x, y, phase;
  integer fd;
  string filename;

  always #5 clk = ~clk; // 100 MHz

  // DUT contract:
  // - pulse_new_row starts one local row evaluation.
  // - pulse_row_done says that row's local candidate is ready and the
  //   three-row input window may advance.
  // - pulse_transaction_pair_done says the two candidate rows and their
  //   inter-row interactions are resolved and both output rows are stable.
  simulator dut (
    .clk                          (clk),
    .pulse_new_row                (pulse_new_row),
    .sim_running                  (sim_running),
    .input_buffers                      (buffers),
    .pulse_row_done               (pulse_row_done),
    .pulse_transaction_pair_done  (pulse_transaction_pair_done),
    .output_buffers                   (res_buffer)
  );

  // Return the res_buffer index selected by the DUT's convention:
  // sending_row = row_index[0] != even_major_evaluation.
  // phase is 0 for even-major and 1 for odd-major evaluation.
 function automatic integer row_output_slot(
    input integer row_y,
    input integer phase_value
);
    row_output_slot = (((row_y % 2) == phase_value) ? 1 : 0);
endfunction

  task automatic pack_source_row(input integer source_y,
                                 output row_buffer_t packed_row);
    integer px;
    begin
      packed_row = '0;
      if (source_y >= 0 && source_y < H) begin
        for (px = 0; px < W; px = px + 1)
          packed_row[px*BitsPerPixel +: BitsPerPixel] = current_grid[source_y][px];
      end
      // Rows outside the visible grid are air (all-zero cell values).
    end
  endtask

  task automatic load_initial_window;
    begin
      pack_source_row(-1, buffers[0]);
      pack_source_row( 0, buffers[1]);
      pack_source_row( 1, buffers[2]);
    end
  endtask

  // Advance [y-1, y, y+1] to [y, y+1, y+2] after row y completes.
  task automatic rotate_input_window(input integer next_center_y);
    begin
      buffers[0] = buffers[1];
      buffers[1] = buffers[2];
      pack_source_row(next_center_y + 1, buffers[2]);
    end
  endtask

  task automatic start_row(input integer row_y);
    begin
      // The current buffers must already contain rows row_y-1, row_y, row_y+1.
      @(negedge clk);
      pulse_new_row = 1'b1;
      @(negedge clk);
      pulse_new_row = 1'b0;
    end
  endtask

  task automatic wait_for_row_done(input integer row_y);
    integer cycles;
    begin
      cycles = 0;
      while (pulse_row_done !== 1'b1 && cycles < MAX_ROW_WAIT_CYCLES) begin
        @(negedge clk);
        cycles = cycles + 1;
      end
      if (pulse_row_done !== 1'b1)
        $fatal(1, "Timeout waiting for pulse_row_done at row %0d after %0d cycles",
               row_y, cycles);
    end
  endtask

  task automatic wait_for_pair_done(input integer first_row_y,
                                    input integer second_row_y);
    integer cycles;
    begin
      cycles = 0;
      while (pulse_transaction_pair_done !== 1'b1 &&
             cycles < MAX_PAIR_WAIT_CYCLES) begin
        @(negedge clk);
        cycles = cycles + 1;
      end
      if (pulse_transaction_pair_done !== 1'b1)
        $fatal(1,
          "Timeout waiting for transaction pair rows %0d/%0d after %0d cycles",
          first_row_y, second_row_y, cycles);
    end
  endtask

  // In a matched pair, the first row is in res_buffer[0] and the second
  // row is in res_buffer[1]. This holds in both alternating pair alignments.
task automatic capture_pair(input integer first_output_y);
    integer px;
    begin
        for (px = 0; px < W; px = px + 1) begin
            // Upper / first row
            next_grid[first_output_y][px] =
                res_buffer[1][px*BitsPerPixel +: BitsPerPixel];

            // Lower / second row
            next_grid[first_output_y+1][px] =
                res_buffer[0][px*BitsPerPixel +: BitsPerPixel];
        end
    end
endtask
  // Odd-major alignment has unpaired top and bottom edge rows. Capture those
  // single-row local results from whichever output slot the DUT selected.
  task automatic capture_single_row(input integer row_y,
                                    input integer phase_value);
    integer px;
    integer slot;
    begin
      slot = row_output_slot(row_y, phase_value);
      for (px = 0; px < W; px = px + 1)
        next_grid[row_y][px] =
          res_buffer[slot][px*BitsPerPixel +: BitsPerPixel];
    end
  endtask

  // A colourful renderer with subtle spatial and cell-age variation.
  function automatic logic [23:0] pixel_rgb(input logic [7:0] cell_value,
                                             input integer px,
                                             input integer py);
    logic [3:0] mat;
    integer age;
    integer shade;
    integer rr, gg, bb;
    begin
      mat = cell_value[7:4];
      age = cell_value[3:0];
      shade = ((px*3 + py*5 + age*11) % 25) - 12;
      rr = 0;
      gg = 0;
      bb = 0;

      case (mat)
        MatStone: begin rr=112; gg=123; bb=145; shade=shade/2; end
        MatSand:  begin rr=238; gg=184; bb=75;  end
        MatWater: begin rr=28;  gg=125; bb=238; shade=shade+5; end
        MatLava:  begin rr=255; gg=76;  bb=18;  shade=shade+8; end
        MatAcid:  begin rr=105; gg=225; bb=55;  shade=shade+4; end
        MatGOL:   begin rr=age[3] ? 226 : 113; gg=age[3] ? 235 : 117; bb=age[3] ? 245 : 122; shade=shade/3; end
        MatExplo: begin rr=60 + age*13; gg=age*11;  bb=age;  shade=0; end
        default:  begin rr=5;   gg=9;   bb=19;  shade=shade/5; end
      endcase

      rr = rr + shade;
      gg = gg + shade;
      bb = bb + shade;

      if (rr < 0)       rr = 0;
      else if (rr > 255) rr = 255;
      if (gg < 0)       gg = 0;
      else if (gg > 255) gg = 255;
      if (bb < 0)       bb = 0;
      else if (bb > 255) bb = 255;

      pixel_rgb = {rr[7:0], gg[7:0], bb[7:0]};
    end
  endfunction

  task automatic initialize_world;
    integer px, py;
    begin
      for (py = 0; py < H; py = py + 1) begin
        for (px = 0; px < W; px = px + 1) begin
          current_grid[py][px] = {MatAir, 4'h0};
          next_grid[py][px] = {MatAir, 4'h0};

          // Stone floor and a few platforms.
          if (py >= H-12 || (py == H-65 && px > 70 && px < 220) ||
              (py == H-105 && px > 390 && px < 550))
            current_grid[py][px] = {MatStone, 4'h0};

          // Sand pile.
          if (py > H-95 && py < H-25 && px > 115 && px < 185 &&
              (px-150)*(px-150) + (py-(H-50))*(py-(H-50)) < 1100)
            current_grid[py][px] = {MatSand, 4'h0};

          // Water pool.
          if (py > H-55 && py < H-20 && px > 300 && px < 390)
            current_grid[py][px] = {MatWater, 4'h0};

          // Lava and acid samples.
          if (py > H-45 && py < H-25 && px > 465 && px < 485)
            current_grid[py][px] = {MatLava, 4'h0};
          if (py > H-35 && py < H-25 && px > 520 && px < 540)
            current_grid[py][px] = {MatAcid, 4'h0};
        end
      end

      // A small Game-of-Life seed.
      current_grid[H-40][150] = {MatGOL, 4'b1000};
      current_grid[H-40][152] = {MatGOL, 4'b1000};
      current_grid[H-39][151] = {MatGOL, 4'b1000};
      current_grid[H-39][152] = {MatGOL, 4'b1000};
      current_grid[H-38][151] = {MatGOL, 4'b1000};
      // Explosion seed: maximum initial force, placed in open space.
      current_grid[90][350] = {MatExplo, 4'hF};
    end
  endtask

  task automatic load_world_file(input string path);
    integer input_fd, bytes_read, px, py;
    begin
      input_fd = $fopen(path, "rb");
      if (input_fd == 0)
        $fatal(1, "Could not open grid file %s", path);

      bytes_read = $fread(input_bytes, input_fd);
      if (bytes_read != W*H || $fgetc(input_fd) != -1)
        $fatal(1, "Grid file %s must contain exactly %0d bytes (read %0d)",
               path, W*H, bytes_read);
      $fclose(input_fd);

      for (py = 0; py < H; py = py + 1)
        for (px = 0; px < W; px = px + 1) begin
          current_grid[py][px] = input_bytes[py*W + px];
          next_grid[py][px] = {MatAir, 4'h0};
        end
      $display("Loaded initial grid from %s", path);
    end
  endtask

  task automatic write_ppm(input integer frame_id);
    integer px, py;
    logic [23:0] rgb;
    begin
      $sformat(filename, "%s/frame_%04d.ppm", DUMPDIR, frame_id);
      fd = $fopen(filename, "w");
      if (fd == 0) $fatal(1, "Could not open %s", filename);
      $fwrite(fd, "P3\n%0d %0d\n255\n", W, H);
      for (py = 0; py < H; py = py + 1) begin
        for (px = 0; px < W; px = px + 1) begin
          rgb = pixel_rgb(current_grid[py][px], px, py);
          $fwrite(fd, "%0d %0d %0d\n", rgb[23:16], rgb[15:8], rgb[7:0]);
        end
      end
      $fclose(fd);
      $display("Wrote %s", filename);
    end
  endtask

  task automatic run_one_generation(input integer phase_value);
    integer row_y;
    integer px;
    integer slot;
    logic is_unpaired_boundary;
    begin
      // Preserve the existing grid until every row has been replaced.
      for (row_y = 0; row_y < H; row_y = row_y + 1)
        for (px = 0; px < W; px = px + 1)
          next_grid[row_y][px] = current_grid[row_y][px];

      load_initial_window();

      for (row_y = 0; row_y < H; row_y = row_y + 1) begin
        start_row(row_y);
        wait_for_row_done(row_y);

        is_unpaired_boundary = (phase_value != 0) &&
                               ((row_y == 0) || (row_y == H-1));
        slot = row_output_slot(row_y, phase_value);

        if (is_unpaired_boundary) begin
          // No vertical transaction partner exists at this edge in odd phase.
          capture_single_row(row_y, phase_value);
        end else if (slot == 0) begin
          // This row closes the current pair. Pair completion may be asserted
          // on the same cycle as row_done or in a later resolver cycle.
          wait_for_pair_done(row_y-1, row_y);
          capture_pair(row_y-1);
        end
        // slot == 0 means this is the first row of a pair. Keep its candidate
        // in res_buffer[0] while rotating the source window for its partner.

        if (row_y < H-1)
          rotate_input_window(row_y + 1);
      end

      for (row_y = 0; row_y < H; row_y = row_y + 1)
        for (px = 0; px < W; px = px + 1)
          current_grid[row_y][px] = next_grid[row_y][px];
    end
  endtask

  initial begin
    pulse_new_row = 1'b0;
    sim_running = 1'b0;
    phase = 0; // Must match the DUT's initial even_major_evaluation value.

    for (x = 0; x < 3; x = x + 1)
      buffers[x] = '0;

    if (GRID_FILE != "")
      load_world_file(GRID_FILE);
    else
      initialize_world();
    repeat (5) @(negedge clk);
    sim_running = 1'b1;

    write_ppm(0);
    for (frame_number = 1; frame_number <= NUM_GENERATIONS;
         frame_number = frame_number + 1) begin
      $display("Starting generation %0d (phase=%0d)", frame_number, phase);
      run_one_generation(phase);
      write_ppm(frame_number);
      $display("Completed generation %0d", frame_number);
      phase = !phase;
    end

    $finish;
  end
endmodule
