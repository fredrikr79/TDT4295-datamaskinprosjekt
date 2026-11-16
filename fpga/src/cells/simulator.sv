
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
  localparam int Stride = sim_pkg::CellGridWidth / sim_pkg::SimulationCoreCount;
  localparam int BaseW = (Stride > 1) ? $clog2(Stride) : 1;  // 6
  localparam int RowW = (sim_pkg::CellGridHeight > 1) ? $clog2(sim_pkg::CellGridHeight) : 1;  // 7
  localparam int GolW = (sim_pkg::GolRefreshDelay > 1) ? $clog2(sim_pkg::GolRefreshDelay) : 1;
  // Each core only ever touches its own Stride pixels plus one on each side.
  localparam int WinPx = Stride + 2;
  localparam int WinBits = WinPx * sim_pkg::BitsPerPixel;

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
  typedef struct {
    sim_pkg::neighbourhood_t nb[sim_pkg::SimulationCoreCount];
    logic [BaseW-1:0] base;
    logic valid;
    logic last;
  } pipeline_stage_t;
  pipeline_stage_t setup_stage;

  // [PIPELINE 2] Second register stage. The explosion and GoL rules only look at the
  // registered neighbourhood, never at output_buffers, so they are computed here and
  // registered. Gravity, fluid and the writes run one cycle later from the *_r copies.
  // Results are unchanged; a row takes Stride+2 cycles.
  pipeline_stage_t rule_stage;
  sim_pkg::neighbourhood_t nb_r[sim_pkg::SimulationCoreCount];
  logic [7:0] explosion_r[sim_pkg::SimulationCoreCount];
  logic [7:0] gol_r[sim_pkg::SimulationCoreCount];
  logic [BaseW-1:0] base_r = '0;
  logic valid_r = 1'b0;
  logic last_r = 1'b0;

  // [WRITE STAGE] write slots from the always_comb block near the end of the file:
  // [core][buffer][slot], slot 0 = column-1, 1 = this column, 2 = column+1.
  sim_pkg::neighbourhood_row_en_t wr_en[sim_pkg::SimulationCoreCount][2];
  sim_pkg::neighbourhood_row_t wr_val[sim_pkg::SimulationCoreCount][2];

  // [CHANGED] was "int gol_refresh_counter = 1" + "gol_refresh_counter % GolRefreshDelay == 0"
  // (a 32-bit modulo). Now a 7-bit counter wrapping at GolRefreshDelay. Starts at 1 like the
  // old counter, so refreshes land on the same frames.
  logic [GolW-1:0] gol_refresh_counter = GolW'(1);
  logic gol_refresh;
  assign gol_refresh = (gol_refresh_counter == '0);

  // [CHANGED] was "int current_row_index"
  row_t current_row_index = '0;
  logic is_odd_major_evaluation = 1'b0;

  // Linear feedback shift register acting as a L/R decision selector.
  // [CHANGED] was "int lfsr" (same value, now unsigned 32-bit)
  logic [31:0] lfsr = 32'hACE1_2345;

  // [CHANGED] was "int lfsr_gol" + sim_pkg::lowbias32() per core (two 32x32 constant
  // multiplies per core). Now one 16-bit LFSR per core, stepped every cycle a row runs.
  // NOTE: the GoL noise is still pseudo-random but the exact sequence differs from before.
  logic [15:0] noise_lfsr[sim_pkg::SimulationCoreCount];
  initial
    for (int c = 0; c < sim_pkg::SimulationCoreCount; c++)
      noise_lfsr[c] = 16'hACE1 ^ 16'(c * 16'h3B5D + 1);

  // Write transaction sends to res_buffer 0 and recieves to 1
  logic is_sending_row;
  assign is_sending_row = current_row_index[0] == is_odd_major_evaluation;

  logic is_unmatched_boundry_row;
  // calculation here only works for even CellGridHeights
  // [CHANGED] comparisons typed for the narrow row_t
  assign is_unmatched_boundry_row = is_odd_major_evaluation &&
    (current_row_index == '0 || current_row_index == row_t'(sim_pkg::CellGridHeight - 1));


  always @(posedge clk) begin

    // Drive signals (output buffers must be owned by this block)


    // [PIPELINE] Stage 1: read each core's neighbourhood from input_buffers and register it,
    // then advance the issue counter. Only the 40:1 window mux is in front of these registers.
    setup_stage.valid <= is_row_running;
    setup_stage.last  <= is_row_running && (simulation_base == BaseW'(Stride - 1));
    setup_stage.base  <= simulation_base;
    for (int core = 0; core < sim_pkg::SimulationCoreCount; core++)
    setup_stage.nb[core] <= setup_neighbourhood(core);

    if (setup_stage.valid)
      for (int c = 0; c < sim_pkg::SimulationCoreCount; c++)
      noise_lfsr[c] <= {
        noise_lfsr[c][14:0],
        noise_lfsr[c][15] ^ noise_lfsr[c][13] ^ noise_lfsr[c][12] ^ noise_lfsr[c][10]
      };


    // ---- [PIPELINE 2] Stage 2: explosion and GoL rules from the registered neighbourhood ----
    rule_stage.valid <= setup_stage.valid;
    rule_stage.last <= setup_stage.last;
    rule_stage.base <= setup_stage.base;
    for (int core = 0; core < sim_pkg::SimulationCoreCount; core++) begin
      rule_stage.nb[core] <= setup_stage.nb[core];
      // The two random decisions come from this core's LFSR (each ~1/4 chance).
      explosion_r[core] <= sim_rules::explosion_rules(
          setup_stage.nb[core],
          noise_lfsr[core][3] & noise_lfsr[core][11],
          noise_lfsr[core][6] & noise_lfsr[core][14]
      );
      gol_r[core] <= sim_rules::gol_rules(
          setup_stage.nb[core], simulation_counter, gol_refresh, noise_lfsr[core][0]
      );
    end

    // Step the per-core noise LFSRs once per pixel.

    // ---- Stage 3 below works on base_r / nb_r / valid_r ----


    // [WRITE STAGE] Apply the write slots worked out in the always_comb block below.
    // Each output pixel can only be written from three places: its own column (offset 0),
    // the column to its left writing +1, or the column to its right writing -1. Those are
    // fixed for each column, so this is three small compares per pixel instead of the
    // previous 16 write calls x 40 positions per core.
    if (rule_stage.valid && sim_running) begin
      for (int b = 0; b < 2; b++) begin
        for (int column = 0; column < sim_pkg::CellGridWidth; column++) begin
          if (rule_stage.base == BaseW'(column % Stride) && wr_en[column/Stride][b].center)
            output_buffers[b][column*sim_pkg::BitsPerPixel+:sim_pkg::BitsPerPixel] <= wr_val[column/Stride][b].center;
          else if (column > 0 && rule_stage.base == BaseW'((column - 1) % Stride) &&  wr_en[(column-1)/Stride][b].left)
            output_buffers[b][column*sim_pkg::BitsPerPixel+:sim_pkg::BitsPerPixel] <= wr_val[(column-1)/Stride][b].left;
          else if (column < sim_pkg::CellGridWidth - 1 && rule_stage.base == BaseW'((column + 1) % Stride) && wr_en[(column+1)/Stride][b].right)
            output_buffers[b][column*sim_pkg::BitsPerPixel+:sim_pkg::BitsPerPixel] <= wr_val[(column+1)/Stride][b].right;
        end
      end
    end

    // advancement logic
    // [PIPELINE] end-of-row work now happens when stage 2 finishes the last pixel.
    // simulation_base / is_row_running are advanced by stage 1 above.
    begin
      pulse_transaction_pair_done <= 1'b0;
      pulse_row_done <= 1'b0;
      if (rule_stage.valid && rule_stage.last) begin
        pulse_row_done <= 1'b1;

        if (!is_sending_row || is_unmatched_boundry_row) begin
          pulse_transaction_pair_done <= 1'b1;
          lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        end

        // [CHANGED] compare typed for row_t
        if (current_row_index == row_t'(sim_pkg::CellGridHeight - 1)) begin
          current_row_index <= '0;
          simulation_counter <= simulation_counter + 1;

          // [CHANGED] was gol_refresh_counter <= gol_refresh_counter + 1
          gol_refresh_counter <= (gol_refresh_counter == GolW'(sim_pkg::GolRefreshDelay - 1)) ? '0 : gol_refresh_counter + 1'b1;
          // [REMOVED] lfsr_gol step (lfsr_gol no longer exists)

          is_odd_major_evaluation <= ~is_odd_major_evaluation;
        end else begin
          current_row_index <= current_row_index + 1'b1;
        end
      end
      if (pulse_frame_restart) current_row_index <= '0;

      if (pulse_new_row) begin
        if (is_unmatched_boundry_row) begin
          output_buffers[is_sending_row] <= input_buffers[1];
        end else if (is_sending_row) begin
          // First row of a transaction pair
          output_buffers[1] <= input_buffers[1];
          output_buffers[0] <= input_buffers[2];
        end

        is_row_running <= 1'b1;
      end

      if (is_row_running) begin
        if (simulation_base == BaseW'(Stride - 1)) begin
          simulation_base <= '0;
          is_row_running  <= 1'b0;  // stop issuing; stage 2 still has the last pixel
        end else begin
          simulation_base <= simulation_base + 1'b1;
        end
      end
    end

  end

  // [WRITE STAGE] Per-core decision for the pixel in stage 3, as write slots.
  //   wr_en / wr_val [core][buffer][slot], slot 0 = column-1, 1 = this column, 2 = column+1.
  // Priority is unchanged: explosion, GoL, gravity, fluid. The rows are named by role first
  // ("mine" = the row being processed, output buffer is_sending_row; "other" = the other
  // output buffer) and mapped onto the two physical buffers at the end.
  always_comb begin
    for (int core = 0; core < sim_pkg::SimulationCoreCount; core++) begin
      logic at_left_edge, at_right_edge;
      sim_pkg::neighbourhood_t nb;
      sim_pkg::neighbourhood_row_t out_upper, out_lower;
      sim_pkg::gravity_movement_t gravity_movement;
      sim_pkg::fluid_movement_t   fluid_movement;
      sim_pkg::neighbourhood_row_en_t sending_en, receiving_en;
      sim_pkg::neighbourhood_row_t sending_val, receiving_val;

      at_left_edge = (core == 0) && (rule_stage.base == '0);
      at_right_edge = (core == sim_pkg::SimulationCoreCount - 1) && (rule_stage.base == BaseW'(Stride - 1));
      nb = rule_stage.nb[core];
      out_upper = out_slice(output_buffers[1], core);
      out_lower = out_slice(output_buffers[0], core);

      gravity_movement = sim_pkg::DownNone;
      fluid_movement = '0;
      if (!is_sending_row && !is_unmatched_boundry_row) begin
        gravity_movement = sim_rules::gravity_rules(out_upper, out_lower, nb, lfsr[0]);
        fluid_movement = sim_rules::fluid_rules(out_upper, out_lower, nb, lfsr[0], is_sending_row,
                                                at_left_edge, at_right_edge);
      end

      sending_en = 0;
      receiving_en = 0;
      sending_val = 0;
      receiving_val = 0;

      if (explosion_r[core] != 8'hFF) begin
        sending_en.center  = 1'b1;
        sending_val.center = explosion_r[core];
      end else if (gol_r[core] != 8'hFF) begin
        sending_en.center  = 1'b1;
        sending_val.center = gol_r[core];
      end else if (gravity_movement == sim_pkg::DownRight) begin
        sending_en.center   = 1'b1;
        sending_val.center  = nb.topLeft;
        receiving_en.right  = 1'b1;
        receiving_val.right = nb.center;
      end else if (gravity_movement == sim_pkg::Down) begin
        receiving_en.center = 1'b1;
        receiving_val.center = nb.center;
        sending_en.center = 1'b1;
        sending_val.center = nb.top;
      end else if (gravity_movement == sim_pkg::DownLeft) begin
        sending_en.center  = 1'b1;
        sending_val.center = nb.topRight;
        receiving_en.left  = 1'b1;
        receiving_val.left = nb.center;
      end else begin
        // fluid: a top-row move and a centre-row move can both happen
        if (fluid_movement.topLeft) begin
          receiving_en.right   = 1'b1;
          receiving_val.right  = nb.top;
          receiving_en.center  = 1'b1;
          receiving_val.center = nb.topLeft;
        end else if (fluid_movement.topRight) begin
          receiving_en.left = 1'b1;
          receiving_val.left = nb.top;
          receiving_en.center = 1'b1;
          receiving_val.center = nb.topRight;
        end
        if (fluid_movement.centerLeft) begin
          sending_en.right   = 1'b1;
          sending_val.right  = nb.center;
          sending_en.center  = 1'b1;
          sending_val.center = nb.left;
        end else if (fluid_movement.centerRight) begin
          sending_en.left = 1'b1;
          sending_val.left = nb.center;
          sending_en.center = 1'b1;
          sending_val.center = nb.right;
        end
      end

      // role -> physical buffer: "mine" is output buffer is_sending_row
      for (int b = 0; b < 2; b++) begin
        if (is_sending_row == 1'(b)) begin
          wr_en[core][b].left = sending_en.left;
          wr_en[core][b].right = sending_en.right;
          wr_en[core][b].center = sending_en.center;
          wr_val[core][b].left = sending_val.left;
          wr_val[core][b].right = sending_val.right;
          wr_val[core][b].center = sending_val.center;
        end else begin
          wr_en[core][b].left = receiving_en.left;
          wr_en[core][b].right = receiving_en.right;
          wr_en[core][b].center = receiving_en.center;
          wr_val[core][b].left = receiving_val.left;
          wr_val[core][b].right = receiving_val.right;
          wr_val[core][b].center = receiving_val.center;
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
      if (c >= 0 && c < sim_pkg::CellGridWidth)
        w[p*sim_pkg::BitsPerPixel+:sim_pkg::BitsPerPixel] = row[c*sim_pkg::BitsPerPixel+:sim_pkg::BitsPerPixel];
    end
    return w;
  endfunction

  // [TIMING] replaces mask_to_core. Left/centre/right pixels of `row` around this core's
  // stage-2 pixel (base_r), as one 24-bit slice: {right, centre, left}.
  function automatic logic [3*sim_pkg::BitsPerPixel-1:0] out_slice(input sim_pkg::row_buffer_t row,
                                                                   input int core);
    window_t w = window(row, core);
    return w[rule_stage.base*sim_pkg::BitsPerPixel+:3*sim_pkg::BitsPerPixel];
  endfunction

  // [CHANGED] Rewritten. Was: setup_neighbourhood(int column), nine separate
  // input_buffers[k][bit_column +: 8] reads with 32-bit indexes (each a 160:1 mux) plus
  // column > 0 / column < sim_pkg::CellGridWidth-1 edge checks.
  // Now: cut the core's 42-pixel window out with constant indexes, then take
  // left/centre/right as one 24-bit slice selected by the 6-bit simulation_base
  // (~40:1 mux). Edge checks are handled by window() padding with 0 (MatAir).
  // Row checks (top/bottom) are unchanged.
  function automatic sim_pkg::neighbourhood_t setup_neighbourhood(input int core);
    sim_pkg::neighbourhood_t n = '0;  // every field MatAir
    window_t window_top = window(input_buffers[0], core);
    window_t window_center = window(input_buffers[1], core);
    window_t window_bottom = window(input_buffers[2], core);

    // Window position simulation_base+1 is the centre pixel, so one 3-pixel slice starting
    // at simulation_base gives left, centre, right.
    {n.right, n.center, n.left} = window_center[simulation_base*sim_pkg::BitsPerPixel+:3*sim_pkg::BitsPerPixel];

    if (current_row_index > 0)
      {n.topRight, n.top, n.topLeft} = window_top[simulation_base*sim_pkg::BitsPerPixel+:3*sim_pkg::BitsPerPixel];

    if (current_row_index < row_t'(sim_pkg::CellGridHeight - 1))
      {n.bottomRight, n.bottom, n.bottomLeft} = window_bottom[simulation_base*sim_pkg::BitsPerPixel+:3*sim_pkg::BitsPerPixel];

    return n;
  endfunction


endmodule

