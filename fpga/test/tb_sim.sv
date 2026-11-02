/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_sim;
  import sim_pkg::*;

  localparam int W = CellGridWidth;
  localparam int H = CellGridHeight;
  localparam int PAIRS = (H + 1) / 2;
  localparam int MAX_WAIT_CYCLES = 2_000_000;

  logic clk = 1'b0;
  logic pulse_new_row = 1'b0;
  logic sim_running = 1'b0;
  row_buffer_t buffers [3];
  row_buffer_t res_buffer [2];
  logic pulse_transaction_pair_done;

  // Framebuffers store the complete 8-bit cell (material[7:4], counter[3:0]).
  logic [7:0] current_grid [0:H-1][0:W-1];
  logic [7:0] next_grid    [0:H-1][0:W-1];

  integer frame_number = 0;
  integer x, y, pair_y, wait_cycles;
  integer fd;
  string filename;

  always #5 clk = ~clk; // 100 MHz

  // This is the intended transaction-pair interface. Rename the completion
  // port here if your final DUT uses a different spelling.
  simulator dut (
    .clk                         (clk),
    .pulse_new_row               (pulse_new_row),
    .sim_running                 (sim_running),
    .buffers                     (buffers),
    .pulse_transaction_pair_done(pulse_transaction_pair_done),
    .res_buffer                  (res_buffer)
  );

  // Build a three-row source window. Out-of-screen rows are air.
  task automatic pack_source_row(input integer source_y, output row_buffer_t packed_row);
    integer px;
    begin
      packed_row = '0;
      if (source_y >= 0 && source_y < H) begin
        for (px = 0; px < W; px = px + 1)
          packed_row[px*BitsPerPixel +: BitsPerPixel] = current_grid[source_y][px];
      end
    end
  endtask

  task automatic load_transaction_window(input integer first_output_y);
    begin
      pack_source_row(first_output_y - 1, buffers[0]);
      pack_source_row(first_output_y,     buffers[1]);
      pack_source_row(first_output_y + 1, buffers[2]);
    end
  endtask

  // Small deterministic starting world. Replace this task with a file loader
  // or randomized seed once the simulator's world loader is available.
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

      // A small Game-of-Life seed, placed away from the material samples.
      current_grid[90][270] = {MatGOL, 4'h0};
      current_grid[90][271] = {MatGOL, 4'h0};
      current_grid[90][272] = {MatGOL, 4'h0};
    end
  endtask

  // Vivid material palette with gentle per-pixel/counter variation.
  // Returns 24-bit 0xRRGGBB. Air is near-black with a subtle blue tint.
  function automatic logic [23:0] pixel_rgb(input logic [7:0] cell_val, input integer px, input integer py);
    logic [3:0] mat, age;
    integer shade;
    integer rr, gg, bb;
    begin
      mat = cell_val[7:4];
      age = cell_val[3:0];
      shade = ((px*3 + py*5 + age*11) % 25) - 12;
      rr = 0; gg = 0; bb = 0;
      case (mat)
        MatStone: begin rr=112; gg=123; bb=145; shade=shade/2; end
        MatSand:  begin rr=238; gg=184; bb=75;  end
        MatWater: begin rr=28;  gg=125; bb=238; shade=shade+5; end
        MatLava:  begin rr=255; gg=76;  bb=18;  shade=shade+8; end
        MatAcid:  begin rr=105; gg=225; bb=55;  shade=shade+4; end
        MatGOL:   begin rr=226; gg=235; bb=245; shade=shade/3; end
        MatExplo: begin rr=255; gg=45;  bb=65;  shade=shade+age*2; end
        default:  begin rr=5;   gg=9;   bb=19;  shade=shade/5; end
      endcase
      rr = rr + shade; gg = gg + shade; bb = bb + shade;
      if (rr < 0) rr=0; else if (rr > 255) rr=255;
      if (gg < 0) gg=0; else if (gg > 255) gg=255;
      if (bb < 0) bb=0; else if (bb > 255) bb=255;
      pixel_rgb = {8'(rr), 8'(gg), 8'(bb)};
    end
  endfunction

  task automatic write_ppm(input integer frame_id);
    integer px, py;
    logic [23:0] rgb;
    begin
      $sformat(filename, "frame_%04d.ppm", frame_id);
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

  task automatic capture_pair(input integer first_output_y);
    integer px;
    begin
      // Contract: res_buffer[0] is first_output_y and [1] is first_output_y+1.
      for (px = 0; px < W; px = px + 1) begin
        next_grid[first_output_y][px] = res_buffer[0][px*BitsPerPixel +: BitsPerPixel];
        if (first_output_y + 1 < H)
          next_grid[first_output_y+1][px] = res_buffer[1][px*BitsPerPixel +: BitsPerPixel];
      end
    end
  endtask

  task automatic run_one_generation;
    integer py, cycles;
    begin
      for (py = 0; py < H; py = py + 1)
        for (x = 0; x < W; x = x + 1)
          next_grid[py][x] = {MatAir, 4'h0};

      for (py = 0; py < H; py = py + 2) begin
        load_transaction_window(py);
        @(negedge clk);
        pulse_new_row = 1'b1;
        @(negedge clk);
        pulse_new_row = 1'b0;

        cycles = 0;
        // Wait for the DUT's pair-level completion pulse.
        while (pulse_transaction_pair_done !== 1'b1 && cycles < MAX_WAIT_CYCLES) begin
          @(posedge clk);
          cycles = cycles + 1;
        end
        if (cycles >= MAX_WAIT_CYCLES)
          $fatal(1, "Timeout waiting for transaction pair at rows %0d/%0d", py, py+1);

        capture_pair(py);
        // Move away from the completion edge before starting the next pair.
        @(negedge clk);
      end

      for (py = 0; py < H; py = py + 1)
        for (x = 0; x < W; x = x + 1)
          current_grid[py][x] = next_grid[py][x];
    end
  endtask

  initial begin
    pulse_new_row = 1'b0;
    sim_running = 1'b0;
    for (x = 0; x < 3; x = x + 1) buffers[x] = '0;
    initialize_world();
    repeat (5) @(negedge clk);
    sim_running = 1'b1;

    write_ppm(0);
    for (frame_number = 1; frame_number <= 10; frame_number = frame_number + 1) begin
      run_one_generation();
      write_ppm(frame_number);
      $display("Completed generation %0d", frame_number);
    end
    $finish;
  end
endmodule
