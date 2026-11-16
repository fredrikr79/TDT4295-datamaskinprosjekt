package sim_rules;

  // [SYNTH FIX 3] Material properties are read through sim_pkg::mat_<property>(material)
  // instead of sim_pkg::props(material).<property>. Selecting a field straight off a function
  // call result was synthesized with undriven fields (Synth 8-3848), so "falls", "spreads",
  // "displaceable" and "solid" all read as 0 in hardware and nothing moved.

  // [TIMING 2] Explosion rule rewritten for speed. Same result as before, but:
  //  - 6-bit signed arithmetic instead of 32-bit int (every compare was a 32-bit carry chain)
  //  - the eight neighbour candidates are computed side by side and reduced with a max tree,
  //    instead of eight compare-and-update steps one after another.
  // The old form was ~38 ns of logic between nb_q and output_buffers on a 10 ns clock.
  //   can_move_free : ~1/4 chance, makes diagonal steps lossless
  //   diagonal_cheap: ~1/4 chance, diagonal cost 2 instead of 3
  typedef logic signed [5:0] force_t;  // range needed: -6 .. 15

  // Strength a neighbour hands to this cell; 0 (no effect) if it is not an explosion.
  function automatic force_t explo_candidate(input logic [7:0] neighbour, input logic [1:0] cost,
                                             input logic [3:0] resistance);
    force_t strength;
    strength = force_t'({2'b00, neighbour[3:0]}) - force_t'({4'b0000, cost})
             - force_t'({2'b00, resistance});
    return (neighbour[7:4] == sim_pkg::MatExplo) ? strength : 6'sd0;  // single return
  endfunction

  function automatic force_t force_max2(input force_t a, input force_t b);
    return (a > b) ? a : b;
  endfunction

  function automatic logic [7:0] explosion_rules(
      input sim_pkg::neighbourhood_t stencil, input logic can_move_free,
      input logic diagonal_cheap);

    logic [3:0] blast_resistance = sim_pkg::mat_blast_resistance(stencil.center[7:4]);
    logic center_is_explo = (stencil.center[7:4] == sim_pkg::MatExplo);
    logic [1:0] diagonal_cost = can_move_free ? 2'd0 : (diagonal_cheap ? 2'd2 : 2'd3);

    // Straight neighbours always cost 2; diagonals cost diagonal_cost.
    force_t c_top = explo_candidate(stencil.top, 2'd2, blast_resistance);
    force_t c_left = explo_candidate(stencil.left, 2'd2, blast_resistance);
    force_t c_right = explo_candidate(stencil.right, 2'd2, blast_resistance);
    force_t c_bottom = explo_candidate(stencil.bottom, 2'd2, blast_resistance);
    force_t c_tl = explo_candidate(stencil.topLeft, diagonal_cost, blast_resistance);
    force_t c_tr = explo_candidate(stencil.topRight, diagonal_cost, blast_resistance);
    force_t c_bl = explo_candidate(stencil.bottomLeft, diagonal_cost, blast_resistance);
    force_t c_br = explo_candidate(stencil.bottomRight, diagonal_cost, blast_resistance);

    // Own strength decays by one each pass.
    force_t c_center = (center_is_explo && stencil.center[3:0] != 4'd0)
        ? force_t'({2'b00, stencil.center[3:0]}) - 6'sd1 : 6'sd0;

    // Max tree, three levels deep plus the centre.
    force_t m_a = force_max2(force_max2(c_top, c_left), force_max2(c_right, c_bottom));
    force_t m_b = force_max2(force_max2(c_tl, c_tr), force_max2(c_bl, c_br));
    force_t full_force = force_max2(force_max2(m_a, m_b), c_center);

    logic is_exploding =
        center_is_explo ||
        stencil.topLeft[7:4]     == sim_pkg::MatExplo ||
        stencil.top[7:4]         == sim_pkg::MatExplo ||
        stencil.topRight[7:4]    == sim_pkg::MatExplo ||
        stencil.left[7:4]        == sim_pkg::MatExplo ||
        stencil.right[7:4]       == sim_pkg::MatExplo ||
        stencil.bottomLeft[7:4]  == sim_pkg::MatExplo ||
        stencil.bottom[7:4]      == sim_pkg::MatExplo ||
        stencil.bottomRight[7:4] == sim_pkg::MatExplo;

    // [SYNTH FIX] One result variable and one return. The earlier form returned from inside
    // nested ifs and fell through to a final return; the synthesized netlist did not match
    // simulation for that pattern (every non-exploding cell came out as air).
    logic [7:0] result;
    result = 8'hFF;  // no change
    if (is_exploding && full_force > 6'sd0) result = {sim_pkg::MatExplo, full_force[3:0]};
    else if (is_exploding && center_is_explo) result = {sim_pkg::MatAir, 4'd0};
    return result;
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

    // [SYNTH FIX] single result variable, single return
    logic can_live = sim_pkg::mat_solid(stencil.center[7:4])
        && !sim_pkg::mat_spreads(stencil.center[7:4])
        && (neighbour_count == 4'd3 || (neighbour_count == 4'd2 && center_is_alive));
    logic [7:0] result;
    result = 8'hFF;  // no change
    if (can_live) result = {sim_pkg::MatGOL, refresh ? refresh_state : 1'b1, 3'b000};
    else if (stencil.center[7:4] == sim_pkg::MatGOL)
      result = {sim_pkg::MatGOL, refresh ? refresh_state : 1'b0, 3'b000};
    return result;
  endfunction
  // [SYNTH FIX 2] The output-row pixels are passed in as two plain 24-bit values,
  // {right, centre, left}: out_upper from output buffer 1, out_lower from output buffer 0.
  // Was: the whole buffer array passed by `ref` and indexed inside the function, which
  // synthesis did not build the way simulation runs it (nothing ever moved).
  function automatic sim_pkg::gravity_movement_t gravity_rules(
      input sim_pkg::neighbourhood_row_t out_upper, input sim_pkg::neighbourhood_row_t out_middle,
      input sim_pkg::neighbourhood_t stencil, input logic r);

    logic [3:0] center_density = sim_pkg::mat_density(stencil.center[7:4]);

    logic m_n1_n1 = sim_pkg::mat_displaceable(stencil.topLeft[7:4]);
    logic m_n1_0 = sim_pkg::mat_displaceable(stencil.left[7:4]);
    logic m_n1_1 = sim_pkg::mat_displaceable(stencil.bottomLeft[7:4]);
    logic m_0_n1 = sim_pkg::mat_displaceable(stencil.top[7:4]);
    logic m_0_0 = sim_pkg::mat_displaceable(stencil.center[7:4]);
    logic m_0_1 = sim_pkg::mat_displaceable(stencil.bottom[7:4]);
    logic m_1_n1 = sim_pkg::mat_displaceable(stencil.topRight[7:4]);
    logic m_1_0 = sim_pkg::mat_displaceable(stencil.right[7:4]);
    logic m_1_1 = sim_pkg::mat_displaceable(stencil.bottomRight[7:4]);

    logic d_n1_n1 = sim_pkg::mat_density(stencil.topLeft[7:4]) > center_density;
    logic d_0_n1 = sim_pkg::mat_density(stencil.top[7:4]) > center_density;
    logic d_1_n1 = sim_pkg::mat_density(stencil.topRight[7:4]) > center_density;
    logic d_n1_1 = sim_pkg::mat_density(stencil.bottomLeft[7:4]) < center_density;
    logic d_0_1 = sim_pkg::mat_density(stencil.bottom[7:4]) < center_density;
    logic d_1_1 = sim_pkg::mat_density(stencil.bottomRight[7:4]) < center_density;

    logic unchanged_n1_n1 =
        stencil.topLeft == out_upper.left;
    logic unchanged_0_n1 = stencil.top == out_upper.center;
    logic unchanged_1_n1 = stencil.topRight == out_upper.right;
    logic unchanged_0_0 = stencil.center == out_middle.center;

    logic d_topLeft_lte_left = sim_pkg::mat_density(stencil.topLeft[7:4]) <= sim_pkg::mat_density(stencil.left[7:4]);
    logic d_topRight_lte_right = sim_pkg::mat_density(stencil.topRight[7:4]) <= sim_pkg::mat_density(stencil.right[7:4]);
    logic d_left_lte_bottomLeft = sim_pkg::mat_density(stencil.left[7:4]) <= sim_pkg::mat_density(stencil.bottomLeft[7:4]);
    logic d_right_lte_bottomRight = sim_pkg::mat_density(stencil.right[7:4]) <= sim_pkg::mat_density(stencil.bottomRight[7:4]);

    logic moveDown = unchanged_0_n1 && unchanged_0_0 && m_0_n1 && d_0_n1;

    logic moveRight = r &&
        unchanged_n1_n1 &&
        unchanged_0_0 &&
        m_n1_n1 &&
        d_n1_n1 &&
        (!m_n1_0 || d_topLeft_lte_left);

    logic moveLeft = !r &&
        unchanged_1_n1 &&
        unchanged_0_0 &&
        m_1_n1 &&
        d_1_n1 &&
        (!m_1_0 || d_topRight_lte_right);

    // [SYNTH FIX] single result variable, single return
    sim_pkg::gravity_movement_t result;
    result = sim_pkg::DownNone;
    if (m_0_0) begin  // Center pixel decides movement timings
      if (moveDown) result = sim_pkg::Down;
      else if (moveRight) result = sim_pkg::DownRight;
      else if (moveLeft) result = sim_pkg::DownLeft;
    end
    return result;
  endfunction


  // [SYNTH FIX 2] same change as gravity_rules: plain 24-bit {right, centre, left} inputs.
  function automatic sim_pkg::fluid_movement_t fluid_rules(
      input sim_pkg::neighbourhood_row_t out_upper,
      input sim_pkg::neighbourhood_row_t out_middle,
      input sim_pkg::neighbourhood_t stencil, input logic r, input logic is_sending_row,
      input logic at_left_edge, input logic at_right_edge);
    // Relies on the fact that every gravity rule has been evaluated prior.

    logic a_0_n1 = stencil.top[7:4] == sim_pkg::MatAir;
    logic a_n1_n1 = stencil.topLeft[7:4] == sim_pkg::MatAir;
    logic a_1_n1 = stencil.topRight[7:4] == sim_pkg::MatAir;
    logic a_0_0 = stencil.center[7:4] == sim_pkg::MatAir;
    logic a_n1_0 = stencil.left[7:4] == sim_pkg::MatAir;
    logic a_1_0 = stencil.right[7:4] == sim_pkg::MatAir;

    logic s_0_n1 = sim_pkg::mat_spreads(stencil.top[7:4]);
    logic s_0_0 = sim_pkg::mat_spreads(stencil.center[7:4]);

    logic top_can_fall = sim_pkg::mat_displaceable(stencil.center[7:4]) && (sim_pkg::mat_falls(stencil.top[7:4]) && (sim_pkg::mat_density(stencil.top[7:4]) > sim_pkg::mat_density(stencil.center[7:4])) || sim_pkg::mat_falls(stencil.topLeft[7:4]) && (sim_pkg::mat_density(stencil.topLeft[7:4]) > sim_pkg::mat_density(stencil.center[7:4])) || sim_pkg::mat_falls(stencil.topRight[7:4]) && (sim_pkg::mat_density(stencil.topRight[7:4]) > sim_pkg::mat_density(stencil.center[7:4])));

    logic center_can_fall = sim_pkg::mat_displaceable(stencil.bottom[7:4]) && (sim_pkg::mat_falls(stencil.center[7:4]) && (sim_pkg::mat_density(stencil.center[7:4]) > sim_pkg::mat_density(stencil.bottom[7:4])) || sim_pkg::mat_falls(stencil.left[7:4]) && (sim_pkg::mat_density(stencil.left[7:4]) > sim_pkg::mat_density(stencil.bottom[7:4])) || sim_pkg::mat_falls(stencil.right[7:4]) && (sim_pkg::mat_density(stencil.right[7:4]) > sim_pkg::mat_density(stencil.bottom[7:4])));

    sim_pkg::fluid_movement_t res = 'b0000;



    logic top_unchanged =
      stencil.top == out_upper.center;
    logic top_left_unchanged = 0, top_right_unchanged = 0;

    logic center_unchanged =
      stencil.center == out_middle.center;
    logic left_unchanged = 0, right_unchanged = 0;

    if (!at_left_edge) begin
      top_left_unchanged =
        stencil.topLeft == out_upper.left;

      left_unchanged = stencil.left == out_middle.left;
    end

    if (!at_right_edge) begin
      top_right_unchanged =
        stencil.topRight == out_upper.right;

      right_unchanged = stencil.right == out_middle.right;
    end

    // If we're a not a sending row, we know our sending row can't
    // issue a transaction. So we can evaluate both rows safely.
    // [SYNTH FIX] was an early "return res;" for sending rows; now gated, single return.

    // Top row
    if (!is_sending_row && !top_can_fall && !a_0_n1 && s_0_n1) begin
      // Incoming
      if (r
          && !at_right_edge
          && a_1_n1
          && top_unchanged
          && top_right_unchanged
      )
        res.topRight = 1;
      else if (!r && !at_left_edge && a_n1_n1 && top_unchanged && top_left_unchanged)
        res.topLeft = 1;
    end

    // Bottom row
    if (!is_sending_row && !center_can_fall && !a_0_0 && s_0_0) begin
      // Incoming
      if (r && !at_right_edge && a_1_0 && center_unchanged && right_unchanged)
        res.centerRight = 1;
      else if (!r && !at_left_edge && a_n1_0 && center_unchanged && left_unchanged)
        res.centerLeft = 1;
    end

    return res;

  endfunction
endpackage
