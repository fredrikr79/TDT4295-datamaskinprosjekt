
// Changes for LUT reduction are tagged [CHANGED] / [REMOVED] / [NEW]. grep "\[CHANGED\]\|\[REMOVED\]\|\[NEW\]"
module simulator #(
) (
    input wire clk,
    pulse_new_row,
    sim_running,
    pulse_frame_restart,
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

  // [PIPELINE 2] Second register stage. The explosion and GoL rules only look at the
  // registered neighbourhood, never at output_buffers, so they are computed here and
  // registered. Gravity, fluid and the writes run one cycle later from the *_r copies.
  // Results are unchanged; a row takes Stride+2 cycles.
  sim_pkg::neighbourhood_t nb_r[Cores];
  logic [7:0] explosion_r[Cores];
  logic [7:0] gol_r[Cores];
  logic [BaseW-1:0] base_r = '0;
  logic valid_r = 1'b0;
  logic last_r = 1'b0;

  // [WRITE STAGE] write slots from the always_comb block near the end of the file:
  // [core][buffer][slot], slot 0 = column-1, 1 = this column, 2 = column+1.
  logic wr_en[Cores][2][3];
  logic [Bpp-1:0] wr_val[Cores][2][3];
 
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
 
    // ---- [PIPELINE 2] Stage 2: explosion and GoL rules from the registered neighbourhood ----
    valid_r <= valid_q;
    last_r  <= last_q;
    base_r  <= base_q;
    for (int core = 0; core < Cores; core++) begin
      nb_r[core] <= nb_q[core];
      // The two random decisions come from this core's LFSR (each ~1/4 chance).
      explosion_r[core] <= sim_rules::explosion_rules(
          nb_q[core],
          noise_lfsr[core][3] & noise_lfsr[core][11],
          noise_lfsr[core][6] & noise_lfsr[core][14]
      );
      gol_r[core] <= sim_rules::gol_rules(
          nb_q[core], simulation_counter, gol_refresh, noise_lfsr[core][0]
      );
    end

    // Step the per-core noise LFSRs once per pixel.
    if (valid_q)
      for (int c = 0; c < Cores; c++)
        noise_lfsr[c] <= {
          noise_lfsr[c][14:0],
          noise_lfsr[c][15] ^ noise_lfsr[c][13] ^ noise_lfsr[c][12] ^ noise_lfsr[c][10]
        };

    // ---- Stage 3 below works on base_r / nb_r / valid_r ----
 
 
    // [WRITE STAGE] Apply the write slots worked out in the always_comb block below.
    // Each output pixel can only be written from three places: its own column (offset 0),
    // the column to its left writing +1, or the column to its right writing -1. Those are
    // fixed for each column, so this is three small compares per pixel instead of the
    // previous 16 write calls x 40 positions per core.
    if (valid_r) begin
      for (int b = 0; b < 2; b++) begin
        for (int c = 0; c < Width; c++) begin
          if (base_r == BaseW'(c % Stride) && wr_en[c/Stride][b][1])
            output_buffers[b][c*Bpp+:Bpp] <= wr_val[c/Stride][b][1];
          if (c > 0) begin
            if (base_r == BaseW'((c - 1) % Stride) && wr_en[(c-1)/Stride][b][2])
              output_buffers[b][c*Bpp+:Bpp] <= wr_val[(c-1)/Stride][b][2];
          end
          if (c < Width - 1) begin
            if (base_r == BaseW'((c + 1) % Stride) && wr_en[(c+1)/Stride][b][0])
              output_buffers[b][c*Bpp+:Bpp] <= wr_val[(c+1)/Stride][b][0];
          end
        end
      end
    end

    // advancement logic
    // [PIPELINE] end-of-row work now happens when stage 2 finishes the last pixel.
    // simulation_base / is_row_running are advanced by stage 1 above.
    begin
      if (valid_r && last_r) begin
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
      if (pulse_frame_restart) current_row_index <= '0;
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
 
  // [TIMING] replaces mask_to_core. Left/centre/right pixels of `row` around this core's
  // stage-2 pixel (base_r), as one 24-bit slice: {right, centre, left}.
  function automatic logic [3*Bpp-1:0] out_slice(input sim_pkg::row_buffer_t row, input int core);
    window_t w = window(row, core);
    return w[base_r*Bpp+:3*Bpp];
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
 
  // [WRITE STAGE] Per-core decision for the pixel in stage 3, as write slots.
  //   wr_en / wr_val [core][buffer][slot], slot 0 = column-1, 1 = this column, 2 = column+1.
  // Priority is unchanged: explosion, GoL, gravity, fluid. The rows are named by role first
  // ("mine" = the row being processed, output buffer is_sending_row; "other" = the other
  // output buffer) and mapped onto the two physical buffers at the end.
  always_comb begin
    for (int core = 0; core < Cores; core++) begin
      logic at_left_edge, at_right_edge;
      sim_pkg::neighbourhood_t nb;
      logic [3*Bpp-1:0] out_upper, out_lower;
      sim_pkg::gravity_movement_t gravity_movement;
      sim_pkg::fluid_movement_t fluid_movement;
      logic mine_en[3], other_en[3];
      logic [Bpp-1:0] mine_val[3], other_val[3];

      at_left_edge = (core == 0) && (base_r == '0);
      at_right_edge = (core == Cores - 1) && (base_r == BaseW'(Stride - 1));
      nb = nb_r[core];
      out_upper = out_slice(output_buffers[1], core);
      out_lower = out_slice(output_buffers[0], core);

      gravity_movement = sim_pkg::DownNone;
      fluid_movement = '0;
      if (!is_sending_row && !is_unmatched_boundry_row) begin
        gravity_movement = sim_rules::gravity_rules(out_upper, out_lower, nb, lfsr[0]);
        fluid_movement = sim_rules::fluid_rules(
            out_upper, out_lower, nb, lfsr[0], is_sending_row, at_left_edge, at_right_edge
        );
      end

      for (int k = 0; k < 3; k++) begin
        mine_en[k] = 1'b0;
        other_en[k] = 1'b0;
        mine_val[k] = '0;
        other_val[k] = '0;
      end

      if (explosion_r[core] != 8'hFF) begin
        mine_en[1] = 1'b1;
        mine_val[1] = explosion_r[core];
      end else if (gol_r[core] != 8'hFF) begin
        mine_en[1] = 1'b1;
        mine_val[1] = gol_r[core];
      end else if (gravity_movement == sim_pkg::DownRight) begin
        mine_en[1] = 1'b1;
        mine_val[1] = nb.topLeft;
        other_en[0] = 1'b1;
        other_val[0] = nb.center;
      end else if (gravity_movement == sim_pkg::Down) begin
        other_en[1] = 1'b1;
        other_val[1] = nb.center;
        mine_en[1] = 1'b1;
        mine_val[1] = nb.top;
      end else if (gravity_movement == sim_pkg::DownLeft) begin
        mine_en[1] = 1'b1;
        mine_val[1] = nb.topRight;
        other_en[2] = 1'b1;
        other_val[2] = nb.center;
      end else begin
        // fluid: a top-row move and a centre-row move can both happen
        if (fluid_movement.topLeft) begin
          other_en[0] = 1'b1;
          other_val[0] = nb.top;
          other_en[1] = 1'b1;
          other_val[1] = nb.topLeft;
        end else if (fluid_movement.topRight) begin
          other_en[2] = 1'b1;
          other_val[2] = nb.top;
          other_en[1] = 1'b1;
          other_val[1] = nb.topRight;
        end
        if (fluid_movement.centerLeft) begin
          mine_en[0] = 1'b1;
          mine_val[0] = nb.center;
          mine_en[1] = 1'b1;
          mine_val[1] = nb.left;
        end else if (fluid_movement.centerRight) begin
          mine_en[2] = 1'b1;
          mine_val[2] = nb.center;
          mine_en[1] = 1'b1;
          mine_val[1] = nb.right;
        end
      end

      // role -> physical buffer: "mine" is output buffer is_sending_row
      for (int b = 0; b < 2; b++) begin
        for (int k = 0; k < 3; k++) begin
          if (is_sending_row == 1'(b)) begin
            wr_en[core][b][k] = mine_en[k];
            wr_val[core][b][k] = mine_val[k];
          end else begin
            wr_en[core][b][k] = other_en[k];
            wr_val[core][b][k] = other_val[k];
          end
        end
      end
    end
  end

endmodule
 