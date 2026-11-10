package sim_rules;

  function automatic logic [7:0] explosion_rules(
      input sim_pkg::neighbourhood_t stencil, input int x, input int row_index, input int counter);

    int decayed_center;

    int center = stencil.center[7:4] == sim_pkg::MatExplo ? int'(stencil.center[3:0]) : 0;

    int diagonal_cost;
    int grid_cost;
    int proposed_val;

    logic is_exploding;
    int full_force;
    int force_max = 32'd0;
    logic [3:0] force_strength = 4'd0;
    logic [3:0] blast_resistance = sim_pkg::props(stencil.center[7:4]).blast_resistance;


    logic [31:0] temporal_noise;
    logic [31:0] pos_noise;
    logic can_move_free;

    pos_noise = 32'(x) * 32'h9E3779B9 ^ 32'(row_index) * 32'h85EBCA6B;

    pos_noise ^= pos_noise >> 16;
    pos_noise *= 32'h7FEB352D;
    pos_noise ^= pos_noise >> 15;

    temporal_noise = pos_noise ^ (32'(counter) * 32'hC2B2AE35);

    temporal_noise ^= temporal_noise >> 16;
    temporal_noise *= 32'h7FEB352D;
    temporal_noise ^= temporal_noise >> 15;

    can_move_free = temporal_noise[7:0] < 8'd64;

    diagonal_cost = can_move_free ? 0 : (pos_noise[7:0] < 8'd64 ? 2 : 3);

    grid_cost = can_move_free ? 0 : 2;

    // Treat neighbourhood propegation differently to mimic a real explosion
    if (stencil.top[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.top[3:0]) - 2 - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.left[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.left[3:0]) - 2 - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.right[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.right[3:0]) - 2 - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.bottom[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.bottom[3:0]) - 2 - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end

    if (stencil.topLeft[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.topLeft[3:0]) - diagonal_cost - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.topRight[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.topRight[3:0]) - diagonal_cost - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.bottomLeft[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.bottomLeft[3:0]) - diagonal_cost - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end
    if (stencil.bottomRight[7:4] == sim_pkg::MatExplo) begin
      proposed_val = int'(stencil.bottomRight[3:0]) - diagonal_cost - int'(blast_resistance);
      if (proposed_val > force_max) begin
        force_max = proposed_val;
      end
    end

    decayed_center = center > 0 ? center - 1 : 0;

    full_force = force_max > decayed_center ? force_max : decayed_center;

    is_exploding =
        stencil.topLeft[7:4]     == sim_pkg::MatExplo ||
        stencil.top[7:4]         == sim_pkg::MatExplo ||
        stencil.topRight[7:4]    == sim_pkg::MatExplo ||
        stencil.left[7:4]        == sim_pkg::MatExplo ||
        stencil.center[7:4]      == sim_pkg::MatExplo ||
        stencil.right[7:4]       == sim_pkg::MatExplo ||
        stencil.bottomLeft[7:4]  == sim_pkg::MatExplo ||
        stencil.bottom[7:4]      == sim_pkg::MatExplo ||
        stencil.bottomRight[7:4] == sim_pkg::MatExplo;

    if (is_exploding && counter % sim_pkg::props(sim_pkg::MatExplo).delay == 0) begin
      if (full_force > 0) begin
        force_strength = 4'(full_force);
        return {sim_pkg::MatExplo, force_strength};
      end else if (stencil.center[7:4] == sim_pkg::MatExplo) begin
        return {sim_pkg::MatAir, 4'd0};
      end
    end
    return 8'hFF;
  endfunction

  function automatic logic [7:0] gol_rules(input sim_pkg::neighbourhood_t stencil,
                                           input int counter, input logic refresh,
                                           input logic refresh_state);
    logic [3:0] neighbour_count =
        4'(stencil.top[7:4]         == sim_pkg::MatGOL && stencil.top[3]) +
        4'(stencil.bottom[7:4]      == sim_pkg::MatGOL && stencil.bottom[3]) +
        4'(stencil.topLeft[7:4]     == sim_pkg::MatGOL && stencil.topLeft[3]) +
        4'(stencil.left[7:4]        == sim_pkg::MatGOL && stencil.left[3]) +
        4'(stencil.bottomLeft[7:4]  == sim_pkg::MatGOL && stencil.bottomLeft[3]) +
        4'(stencil.topRight[7:4]    == sim_pkg::MatGOL && stencil.topRight[3]) +
        4'(stencil.right[7:4]       == sim_pkg::MatGOL && stencil.right[3]) +
        4'(stencil.bottomRight[7:4] == sim_pkg::MatGOL && stencil.bottomRight[3]);


    logic center_is_alive = stencil.center[7:4] == sim_pkg::MatGOL && stencil.center[3];

    if (counter % sim_pkg::props(sim_pkg::MatGOL).delay != 0) begin
      if (refresh && stencil.center[7:4] == sim_pkg::MatGOL) begin
        return {sim_pkg::MatGOL, refresh_state, 3'b000};
      end else begin
        return 'hFF;
      end
    end
    ;

    if (sim_pkg::props(
            stencil.center[7:4]
        ).solid && !sim_pkg::props(
            stencil.center[7:4]
        ).spreads && (neighbour_count == 4'd3 || neighbour_count == 4'd2 && center_is_alive)) begin
      return {sim_pkg::MatGOL, refresh ? refresh_state : 1'b1, 3'b000};
    end else if (stencil.center[7:4] == sim_pkg::MatGOL) begin
      return {sim_pkg::MatGOL, refresh ? refresh_state : 1'b0, 3'b000};
    end else begin
      return 'hFF;
    end
  endfunction
  function automatic sim_pkg::gravity_movement_t gravity_rules(
      ref sim_pkg::row_buffer_t output_buffers[2], input sim_pkg::neighbourhood_t stencil,
      input logic r, input int curr, input int counter);

    int center_density = sim_pkg::props(stencil.center[7:4]).density;

    logic c_0_0 = counter % sim_pkg::props(stencil.center[7:4]).delay == 0;
    logic c_n1_n1 = counter % sim_pkg::props(stencil.topLeft[7:4]).delay == 0;
    logic c_0_n1 = counter % sim_pkg::props(stencil.top[7:4]).delay == 0;
    logic c_1_n1 = counter % sim_pkg::props(stencil.topRight[7:4]).delay == 0;

    logic m_n1_n1 = sim_pkg::props(stencil.topLeft[7:4]).displaceable;
    logic m_n1_0 = sim_pkg::props(stencil.left[7:4]).displaceable;
    logic m_n1_1 = sim_pkg::props(stencil.bottomLeft[7:4]).displaceable;
    logic m_0_n1 = sim_pkg::props(stencil.top[7:4]).displaceable;
    logic m_0_0 = sim_pkg::props(stencil.center[7:4]).displaceable;
    logic m_0_1 = sim_pkg::props(stencil.bottom[7:4]).displaceable;
    logic m_1_n1 = sim_pkg::props(stencil.topRight[7:4]).displaceable;
    logic m_1_0 = sim_pkg::props(stencil.right[7:4]).displaceable;
    logic m_1_1 = sim_pkg::props(stencil.bottomRight[7:4]).displaceable;

    logic d_n1_n1 = sim_pkg::props(stencil.topLeft[7:4]).density > center_density;
    logic d_0_n1 = sim_pkg::props(stencil.top[7:4]).density > center_density;
    logic d_1_n1 = sim_pkg::props(stencil.topRight[7:4]).density > center_density;
    logic d_n1_1 = sim_pkg::props(stencil.bottomLeft[7:4]).density < center_density;
    logic d_0_1 = sim_pkg::props(stencil.bottom[7:4]).density < center_density;
    logic d_1_1 = sim_pkg::props(stencil.bottomRight[7:4]).density < center_density;

    logic unchanged_n1_n1 =
        stencil.topLeft == output_buffers[1][curr - sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel];
    logic unchanged_0_n1 = stencil.top == output_buffers[1][curr+:sim_pkg::BitsPerPixel];
    logic unchanged_1_n1 =
        stencil.topRight
        == output_buffers[1][curr + sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel];
    logic unchanged_0_0 = stencil.center == output_buffers[0][curr+:sim_pkg::BitsPerPixel];

    logic d_topLeft_lte_left = sim_pkg::props(
        stencil.topLeft[7:4]
    ).density <= sim_pkg::props(
        stencil.left[7:4]
    ).density;
    logic d_topRight_lte_right = sim_pkg::props(
        stencil.topRight[7:4]
    ).density <= sim_pkg::props(
        stencil.right[7:4]
    ).density;
    logic d_left_lte_bottomLeft = sim_pkg::props(
        stencil.left[7:4]
    ).density <= sim_pkg::props(
        stencil.bottomLeft[7:4]
    ).density;
    logic d_right_lte_bottomRight = sim_pkg::props(
        stencil.right[7:4]
    ).density <= sim_pkg::props(
        stencil.bottomRight[7:4]
    ).density;

    logic moveDown = unchanged_0_n1 && unchanged_0_0 && m_0_n1 && d_0_n1 && c_0_n1;

    logic moveRight = r &&
        unchanged_n1_n1 &&
        unchanged_0_0 &&
        m_n1_n1 &&
        d_n1_n1 &&
        (!m_n1_0 || d_topLeft_lte_left) &&
        c_n1_n1;

    logic moveLeft = !r &&
        unchanged_1_n1 &&
        unchanged_0_0 &&
        m_1_n1 &&
        d_1_n1 &&
        (!m_1_0 || d_topRight_lte_right) &&
        c_1_n1;

    if (m_0_0)  // Center pixel decides movement timings
      if (moveDown) return sim_pkg::Down;
      else if (moveRight) return sim_pkg::DownRight;
      else if (moveLeft) return sim_pkg::DownLeft;
    return sim_pkg::DownNone;
  endfunction


  function automatic sim_pkg::fluid_movement_t fluid_rules(
      ref sim_pkg::row_buffer_t output_buffers[2], input sim_pkg::neighbourhood_t stencil,
      input logic r, input int column, input int counter, input logic is_sending_row);
    int center_bit_column = column * sim_pkg::BitsPerPixel;
    int left_bit_column = center_bit_column - sim_pkg::BitsPerPixel;
    int right_bit_column = center_bit_column + sim_pkg::BitsPerPixel;
    // Relies on the fact that every gravity rule has been evaluated prior.
    logic c_0_n1 = counter % sim_pkg::props(stencil.top[7:4]).delay == 0;
    logic c_0_0 = counter % sim_pkg::props(stencil.center[7:4]).delay == 0;

    logic a_0_n1 = stencil.top[7:4] == sim_pkg::MatAir;
    logic a_n1_n1 = stencil.topLeft[7:4] == sim_pkg::MatAir;
    logic a_1_n1 = stencil.topRight[7:4] == sim_pkg::MatAir;
    logic a_0_0 = stencil.center[7:4] == sim_pkg::MatAir;
    logic a_n1_0 = stencil.left[7:4] == sim_pkg::MatAir;
    logic a_1_0 = stencil.right[7:4] == sim_pkg::MatAir;

    logic s_0_n1 = sim_pkg::props(stencil.top[7:4]).spreads;
    logic s_0_0 = sim_pkg::props(stencil.center[7:4]).spreads;

    logic top_can_fall = sim_pkg::props(
        stencil.center[7:4]
    ).displaceable && (sim_pkg::props(
        stencil.top[7:4]
    ).falls && (sim_pkg::props(
        stencil.top[7:4]
    ).density > sim_pkg::props(
        stencil.center[7:4]
    ).density) || sim_pkg::props(
        stencil.topLeft[7:4]
    ).falls && (sim_pkg::props(
        stencil.topLeft[7:4]
    ).density > sim_pkg::props(
        stencil.center[7:4]
    ).density) || sim_pkg::props(
        stencil.topRight[7:4]
    ).falls && (sim_pkg::props(
        stencil.topRight[7:4]
    ).density > sim_pkg::props(
        stencil.center[7:4]
    ).density));

    logic center_can_fall = sim_pkg::props(
        stencil.bottom[7:4]
    ).displaceable && (sim_pkg::props(
        stencil.center[7:4]
    ).falls && (sim_pkg::props(
        stencil.center[7:4]
    ).density > sim_pkg::props(
        stencil.bottom[7:4]
    ).density) || sim_pkg::props(
        stencil.left[7:4]
    ).falls && (sim_pkg::props(
        stencil.left[7:4]
    ).density > sim_pkg::props(
        stencil.bottom[7:4]
    ).density) || sim_pkg::props(
        stencil.right[7:4]
    ).falls && (sim_pkg::props(
        stencil.right[7:4]
    ).density > sim_pkg::props(
        stencil.bottom[7:4]
    ).density));

    sim_pkg::fluid_movement_t res = 'b0000;



    logic top_unchanged =
      stencil.top == output_buffers[1][center_bit_column+:sim_pkg::BitsPerPixel];
    logic top_left_unchanged = 0, top_right_unchanged = 0;

    logic center_unchanged =
      stencil.center == output_buffers[0][center_bit_column+:sim_pkg::BitsPerPixel];
    logic left_unchanged = 0, right_unchanged = 0;

    if (column > 0) begin
      top_left_unchanged =
        stencil.topLeft == output_buffers[1][left_bit_column+:sim_pkg::BitsPerPixel];

      left_unchanged = stencil.left == output_buffers[0][left_bit_column+:sim_pkg::BitsPerPixel];
    end

    if (column < sim_pkg::CellGridWidth - 1) begin
      top_right_unchanged =
        stencil.topRight == output_buffers[1][right_bit_column+:sim_pkg::BitsPerPixel];

      right_unchanged = stencil.right == output_buffers[0][right_bit_column+:sim_pkg::BitsPerPixel];
    end

    // If we're a not a sending row, we know our sending row can't
    // issue a transaction. So we can evaluate both rows safely.
    if (is_sending_row) begin
      return res;
    end

    // Top row
    if (!top_can_fall && c_0_n1 && !a_0_n1 && s_0_n1) begin
      // Incoming
      if (r
          && column < sim_pkg::CellGridWidth - 1
          && a_1_n1
          && top_unchanged
          && top_right_unchanged
      )
        res.topRight = 1;
      else if (!r && column > 0 && a_n1_n1 && top_unchanged && top_left_unchanged) res.topLeft = 1;
    end

    // Bottom row
    if (!center_can_fall && c_0_0 && !a_0_0 && s_0_0) begin
      // Incoming
      if (r && column < sim_pkg::CellGridWidth - 1 && a_1_0 && center_unchanged && right_unchanged)
        res.centerRight = 1;
      else if (!r && column > 0 && a_n1_0 && center_unchanged && left_unchanged) res.centerLeft = 1;
    end

    return res;

  endfunction
endpackage
