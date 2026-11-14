
// Changes for LUT reduction are tagged [CHANGED] / [REMOVED] / [NEW]. grep "\[CHANGED\]\|\[REMOVED\]\|\[NEW\]"
module simulator #(
) (
    input wire clk,
    pulse_new_row,
    sim_running,
    input sim_pkg::row_buffer_t input_buffers[3],
    output logic pulse_transaction_pair_done,
    pulse_row_done,
    output sim_pkg::row_buffer_t output_buffers[2]
);
  // [NEW] Constants and narrow types. Replaces "int simulation_stride".
  localparam int Cores = sim_pkg::SimulationCoreCount;  // 4
  localparam int Width = sim_pkg::CellGridWidth;  // 160
  localparam int Height = sim_pkg::CellGridHeight;  // 90
  localparam int Bpp = sim_pkg::BitsPerPixel;  // 8
  localparam int Stride = Width / Cores;  // 40 pixels per core
  localparam int BaseW = (Stride > 1) ? $clog2(Stride) : 1;  // 6
  localparam int RowW = (Height > 1) ? $clog2(Height) : 1;  // 7
  localparam int GolW = (sim_pkg::GolRefreshDelay > 1) ? $clog2(sim_pkg::GolRefreshDelay) : 1;
  // Each core only ever touches its own Stride pixels plus one on each side.
  localparam int WinPx = Stride + 2;
  localparam int WinBits = WinPx * Bpp;
 
  typedef logic [sim_pkg::GridXBits-1:0] col_t;
  typedef logic [RowW-1:0] row_t;
  typedef logic [WinBits-1:0] window_t;
 
  // [CHANGED] was "int simulation_base". A 32-bit index makes every neighbour read a
  // full-row mux; 6 bits is all it ever needs (0..Stride-1).
  logic [BaseW-1:0] simulation_base = '0;
  // [REMOVED] int simulation_core_index  (loop variable is now declared in the for loop)
  // [REMOVED] int simulation_stride      (now localparam Stride)
  logic is_row_running = 1'b0;
  int simulation_counter = 0;
 
  // [PIPELINE] Register stage between the neighbourhood read and the rules/writes.
  // Breaks the sim_buffers.buffers -> window mux -> rules -> output_buffers path in two.
  // Stage 1 (issue): reads the neighbourhood for simulation_base from input_buffers.
  // Stage 2: runs the rules and writes for base_q one cycle later.
  // Safe because input_buffers do not change while a row is running. A row now takes
  // Stride+1 cycles instead of Stride.
  sim_pkg::neighbourhood_t nb_q[Cores];
  logic [BaseW-1:0] base_q = '0;
  logic valid_q = 1'b0;  // stage 2 has a pixel to process
  logic last_q = 1'b0;  // ...and it is the last pixel of the row
 
  // [CHANGED] was "int gol_refresh_counter = 1" + "gol_refresh_counter % GolRefreshDelay == 0"
  // (a 32-bit modulo). Now a 7-bit counter wrapping at GolRefreshDelay. Starts at 1 like the
  // old counter, so refreshes land on the same frames.
  logic [GolW-1:0] gol_phase = GolW'(1);
  logic gol_refresh;
  assign gol_refresh = (gol_phase == '0);
 
  // [CHANGED] was "int current_row_index"
  row_t current_row_index = '0;
  logic is_odd_major_evaluation = 1'b0;
 
  // Linear feedback shift register acting as a L/R decision selector.
  // [CHANGED] was "int lfsr" (same value, now unsigned 32-bit)
  logic [31:0] lfsr = 32'hACE1_2345;
 
  // [CHANGED] was "int lfsr_gol" + sim_pkg::lowbias32() per core (two 32x32 constant
  // multiplies per core). Now one 16-bit LFSR per core, stepped every cycle a row runs.
  // NOTE: the GoL noise is still pseudo-random but the exact sequence differs from before.
  logic [15:0] noise_lfsr[Cores];
  initial for (int c = 0; c < Cores; c++) noise_lfsr[c] = 16'hACE1 ^ 16'(c * 16'h3B5D + 1);
 
  // Write transaction sends to res_buffer 0 and recieves to 1
  logic is_sending_row;
  assign is_sending_row = current_row_index[0] == is_odd_major_evaluation;
 
  logic is_unmatched_boundry_row;
  // calculation here only works for even CellGridHeights
  // [CHANGED] comparisons typed for the narrow row_t
  assign is_unmatched_boundry_row = is_odd_major_evaluation &&
    (current_row_index == '0 || current_row_index == row_t'(Height - 1));
 
 
  always @(posedge clk) begin
 
    // Drive signals (output buffers must be owned by this block)
 
    pulse_transaction_pair_done <= 1'b0;
    pulse_row_done <= 1'b0;
 
    if (pulse_new_row && sim_running) begin
      if (is_unmatched_boundry_row) begin
        output_buffers[is_sending_row] <= input_buffers[1];
      end else if (is_sending_row) begin
        // First row of a transaction pair
        output_buffers[1] <= input_buffers[1];
        output_buffers[0] <= input_buffers[2];
      end
 
      is_row_running <= 1'b1;
    end
 
    // [PIPELINE] Stage 1: read each core's neighbourhood from input_buffers and register it,
    // then advance the issue counter. Only the 40:1 window mux is in front of these registers.
    valid_q <= is_row_running;
    last_q  <= is_row_running && (simulation_base == BaseW'(Stride - 1));
    base_q  <= simulation_base;
    for (int core = 0; core < Cores; core++) nb_q[core] <= setup_neighbourhood(core);
 
    if (is_row_running) begin
      if (simulation_base == BaseW'(Stride - 1)) begin
        simulation_base <= '0;
        is_row_running  <= 1'b0;  // stop issuing; stage 2 still has the last pixel
      end else begin
        simulation_base <= simulation_base + 1'b1;
      end
    end
 
    // ---- [PIPELINE] Stage 2 below works on base_q / nb_q / valid_q ----
 
    // [NEW] step the per-core noise LFSRs
    // [PIPELINE] stepped with stage 2 (was is_row_running)
    if (valid_q)
      for (int c = 0; c < Cores; c++)
        noise_lfsr[c] <= {
          noise_lfsr[c][14:0],
          noise_lfsr[c][15] ^ noise_lfsr[c][13] ^ noise_lfsr[c][12] ^ noise_lfsr[c][10]
        };
 
    for (int core = 0; core < Cores; core++) begin : process_lane
      // [CHANGED] column built as 8-bit col_t from a constant (core*Stride) + 6-bit base,
      // instead of int arithmetic on a 32-bit simulation_base.
      // [PIPELINE] uses base_q (stage 2) instead of simulation_base
      automatic col_t col = col_t'(core * Stride) + col_t'(base_q);
      automatic int current_pixel_column = int'(col);
      automatic int current_pixel_bit_column = current_pixel_column * Bpp;
      // [CHANGED] was setup_neighbourhood(current_pixel_column); now takes the core number
      // and reads from that core's window (see setup_neighbourhood below).
      // [PIPELINE] registered neighbourhood from stage 1 (was setup_neighbourhood(core) here)
      automatic sim_pkg::neighbourhood_t neighbourhood = nb_q[core];
      // [REMOVED] left_pixel_bit_column / right_pixel_bit_column (unused)
 
      // [NEW] Output buffers with everything outside this core's window forced to 0, so the
      // indexing inside gravity_rules / fluid_rules collapses to a window-sized mux.
      // ASSUMES those rules only read columns within +-1 of the pixel.
      automatic sim_pkg::row_buffer_t masked_output_buffers[2] = '{
          mask_to_core(output_buffers[0], core),
          mask_to_core(output_buffers[1], core)
      };
 
      // [REMOVED] noise = sim_pkg::lowbias32(lfsr_gol ^ current_pixel_bit_column)
 
      // [CHANGED] row index passed as int'(narrow row_t)
      automatic logic [7:0] explosion_update = sim_rules::explosion_rules(
          neighbourhood, current_pixel_column, int'(current_row_index), simulation_counter
      );
      // [CHANGED] refresh flag is gol_refresh (was % GolRefreshDelay), noise is the
      // per-core LFSR bit (was lowbias32 noise[0])
      automatic logic [7:0] gol_update = sim_rules::gol_rules(
          neighbourhood, simulation_counter, gol_refresh, noise_lfsr[core][0]
      );
 
      automatic sim_pkg::gravity_movement_t gravity_movement = sim_pkg::DownNone;
      automatic sim_pkg::fluid_movement_t fluid_movement = 'b0000;
 
      if (!is_sending_row && !is_unmatched_boundry_row) begin
        // [CHANGED] masked_output_buffers instead of output_buffers
        gravity_movement = sim_rules::gravity_rules(masked_output_buffers, neighbourhood, lfsr[0],
                                                    current_pixel_bit_column, simulation_counter);
        // [CHANGED] masked_output_buffers instead of output_buffers
        fluid_movement = sim_rules::fluid_rules(
          masked_output_buffers,
          neighbourhood,
          lfsr[0],
          current_pixel_column,
          simulation_counter,
          is_sending_row
        );
      end
 
      // Update Rules
      // [CHANGED] "if (!is_row_running) continue;" -> "if (is_row_running) begin ... end"
      // [CHANGED] direct output_buffers[..][current_pixel_bit_column +: 8] <= ... writes now go
      // through write_px(), and the movement tasks take the core number instead of the column.
      // [PIPELINE] gated by valid_q (stage 2) instead of is_row_running
      if (valid_q) begin
        if (explosion_update != 8'hFF) write_px(is_sending_row, core, 0, explosion_update);
        else if (gol_update != 8'hFF) write_px(is_sending_row, core, 0, gol_update);
        else if (gravity_movement != sim_pkg::DownNone)
          apply_gravity_movement(gravity_movement, neighbourhood, core);
        else if (!sim_pkg::no_movement(fluid_movement))
          apply_fluid_movement(fluid_movement, neighbourhood, core);
      end
    end
 
    // advancement logic
    // [PIPELINE] end-of-row work now happens when stage 2 finishes the last pixel.
    // simulation_base / is_row_running are advanced by stage 1 above.
    begin
      if (valid_q && last_q) begin
        pulse_row_done  <= 1'b1;
 
        if (!is_sending_row || is_unmatched_boundry_row) begin
          pulse_transaction_pair_done <= 1'b1;
          lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        end
 
        // [CHANGED] compare typed for row_t
        if (current_row_index == row_t'(Height - 1)) begin
          current_row_index <= '0;
          simulation_counter <= simulation_counter + 1;
 
          // [CHANGED] was gol_refresh_counter <= gol_refresh_counter + 1
          gol_phase <= (gol_phase == GolW'(sim_pkg::GolRefreshDelay - 1)) ? '0 : gol_phase + 1'b1;
          // [REMOVED] lfsr_gol step (lfsr_gol no longer exists)
 
          is_odd_major_evaluation <= ~is_odd_major_evaluation;
        end else begin
          current_row_index <= current_row_index + 1'b1;
        end
      end
    end
 
  end
 
  // [NEW] Constant slice of one row for one core: window pixel p = grid column core*Stride-1+p.
  // Pixels off the grid edge read as 0 (MatAir).
  function automatic window_t window(input sim_pkg::row_buffer_t row, input int core);
    window_t w = '0;
    for (int p = 0; p < WinPx; p++) begin
      int c;
      c = core * Stride - 1 + p;
      if (c >= 0 && c < Width) w[p*Bpp+:Bpp] = row[c*Bpp+:Bpp];
    end
    return w;
  endfunction
 
  // [NEW] Same window, but kept at its real position in a full row with everything else 0.
  function automatic sim_pkg::row_buffer_t mask_to_core(input sim_pkg::row_buffer_t row,
                                                         input int core);
    sim_pkg::row_buffer_t m = '0;
    for (int p = 0; p < WinPx; p++) begin
      int c;
      c = core * Stride - 1 + p;
      if (c >= 0 && c < Width) m[c*Bpp+:Bpp] = row[c*Bpp+:Bpp];
    end
    return m;
  endfunction
 
  // [CHANGED] Rewritten. Was: setup_neighbourhood(int column), nine separate
  // input_buffers[k][bit_column +: 8] reads with 32-bit indexes (each a 160:1 mux) plus
  // column > 0 / column < Width-1 edge checks.
  // Now: cut the core's 42-pixel window out with constant indexes, then take
  // left/centre/right as one 24-bit slice selected by the 6-bit simulation_base
  // (~40:1 mux). Edge checks are handled by window() padding with 0 (MatAir).
  // Row checks (top/bottom) are unchanged.
  function automatic sim_pkg::neighbourhood_t setup_neighbourhood(input int core);
    sim_pkg::neighbourhood_t n = '0;  // every field MatAir
    window_t w0 = window(input_buffers[0], core);
    window_t w1 = window(input_buffers[1], core);
    window_t w2 = window(input_buffers[2], core);
 
    // Window position simulation_base+1 is the centre pixel, so one 3-pixel slice starting
    // at simulation_base gives left, centre, right.
    {n.right, n.center, n.left} = w1[simulation_base*Bpp+:3*Bpp];
 
    if (current_row_index > 0) {n.topRight, n.top, n.topLeft} = w0[simulation_base*Bpp+:3*Bpp];
 
    if (current_row_index < row_t'(Height - 1))
      {n.bottomRight, n.bottom, n.bottomLeft} = w2[simulation_base*Bpp+:3*Bpp];
 
    return n;
  endfunction
 
  // [NEW] Write one pixel at (this core's current column + offset). Every target index is a
  // constant, selected by a 6-bit compare on simulation_base, instead of a 32-bit index decode.
  task automatic write_px(input logic buf_sel, input int core, input int offset,
                          input logic [Bpp-1:0] value);
    for (int p = 0; p < Stride; p++) begin
      int c;
      c = core * Stride + p + offset;
      // [PIPELINE] compares base_q (stage 2) instead of simulation_base
      if (base_q == BaseW'(p) && c >= 0 && c < Width)
        output_buffers[buf_sel][c*Bpp+:Bpp] <= value;
    end
  endtask
 
  // [CHANGED] takes core instead of pixel_column; each
  // output_buffers[x][bit_column +: 8] <= v  became  write_px(x, core, offset, v)
  // with offset 0 = centre, -1 = left, +1 = right. Same buffers, same values as before.
  task automatic apply_gravity_movement(sim_pkg::gravity_movement_t gravity_movement,
                                        sim_pkg::neighbourhood_t neighbourhood, int core);
    if (gravity_movement == sim_pkg::DownRight) begin
      write_px(is_sending_row, core, 0, neighbourhood.topLeft);
      write_px(!is_sending_row, core, -1, neighbourhood.center);
    end else if (gravity_movement == sim_pkg::Down) begin
      write_px(!is_sending_row, core, 0, neighbourhood.center);
      write_px(is_sending_row, core, 0, neighbourhood.top);
    end else if (gravity_movement == sim_pkg::DownLeft) begin
      write_px(is_sending_row, core, 0, neighbourhood.topRight);
      write_px(!is_sending_row, core, 1, neighbourhood.center);
    end
  endtask
 
  // [CHANGED] same conversion as apply_gravity_movement
  task automatic apply_fluid_movement(sim_pkg::fluid_movement_t fluid_movement,
                                      sim_pkg::neighbourhood_t neighbourhood, int core);
    if (fluid_movement.topLeft) begin
      write_px(!is_sending_row, core, -1, neighbourhood.top);
      write_px(!is_sending_row, core, 0, neighbourhood.topLeft);
    end else if (fluid_movement.topRight) begin
      write_px(!is_sending_row, core, 1, neighbourhood.top);
      write_px(!is_sending_row, core, 0, neighbourhood.topRight);
    end
    if (fluid_movement.centerLeft) begin
      write_px(is_sending_row, core, -1, neighbourhood.center);
      write_px(is_sending_row, core, 0, neighbourhood.left);
    end else if (fluid_movement.centerRight) begin
      write_px(is_sending_row, core, 1, neighbourhood.center);
      write_px(is_sending_row, core, 0, neighbourhood.right);
    end
  endtask
 
endmodule
 