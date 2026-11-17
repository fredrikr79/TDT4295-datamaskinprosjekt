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
  function automatic force_t explo_candidate(input sim_pkg::pixel_t neighbour,
                                             input logic [1:0] cost, input logic [3:0] resistance);
    force_t strength;
    strength = force_t'({2'b00, neighbour.data}) - force_t'({4'b0000, cost})
             - force_t'({2'b00, resistance});
    return (neighbour.material == sim_pkg::MatExplo) ? strength : 6'sd0;  // single return
  endfunction

  function automatic force_t force_max2(input force_t a, input force_t b);
    return (a > b) ? a : b;
  endfunction

  // [TIMING 3] The explosion rule is split over two clock cycles. In one cycle it was 13 logic
  // levels (diagonal cost mux -> 3-operand subtract -> 3-level max tree -> compare -> mux) and
  // missed 100 MHz by ~3.7 ns. explosion_rules_a() runs the candidates and the first two max
  // levels; its result is registered; explosion_rules_b() finishes the max and picks the
  // result. explosion_rules() chains the two and gives exactly the old result.
  typedef struct packed {
    force_t m_a;       // max of the straight neighbours
    force_t m_b;       // max of the diagonal neighbours
    force_t c_center;  // own decayed strength
    logic is_exploding;
    logic center_is_explo;
  } explo_partial_t;

  function automatic explo_partial_t explosion_rules_a(input sim_pkg::neighbourhood_t stencil,
                                                       input logic can_move_free,
                                                       input logic diagonal_cheap);
    explo_partial_t p;
    logic [3:0] blast_resistance = sim_pkg::mat_blast_resistance(stencil.center.material);
    logic center_is_explo = (stencil.center.material == sim_pkg::MatExplo);
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

    p.m_a = force_max2(force_max2(c_top, c_left), force_max2(c_right, c_bottom));
    p.m_b = force_max2(force_max2(c_tl, c_tr), force_max2(c_bl, c_br));

    // Own strength decays by one each pass.
    p.c_center = (center_is_explo && stencil.center.data != 4'd0)
        ? force_t'({2'b00, stencil.center.data}) - 6'sd1 : 6'sd0;

    p.center_is_explo = center_is_explo;
    p.is_exploding =
        center_is_explo ||
        stencil.topLeft.material     == sim_pkg::MatExplo ||
        stencil.top.material         == sim_pkg::MatExplo ||
        stencil.topRight.material    == sim_pkg::MatExplo ||
        stencil.left.material        == sim_pkg::MatExplo ||
        stencil.right.material       == sim_pkg::MatExplo ||
        stencil.bottomLeft.material  == sim_pkg::MatExplo ||
        stencil.bottom.material      == sim_pkg::MatExplo ||
        stencil.bottomRight.material == sim_pkg::MatExplo;
    return p;
  endfunction

  function automatic sim_pkg::pixel_t explosion_rules_b(input explo_partial_t p);
    force_t full_force = force_max2(force_max2(p.m_a, p.m_b), p.c_center);
    // [SYNTH FIX] One result variable and one return. The earlier form returned from inside
    // nested ifs and fell through to a final return; the synthesized netlist did not match
    // simulation for that pattern (every non-exploding cell came out as air).
    logic [7:0] result;
    result = 8'hFF;  // no change
    if (p.is_exploding && full_force > 6'sd0) result = {sim_pkg::MatExplo, full_force[3:0]};
    else if (p.is_exploding && p.center_is_explo) result = {sim_pkg::MatAir, 4'd0};
    return result;
  endfunction

  // Single-cycle form, same result as the two halves chained.
  function automatic sim_pkg::pixel_t explosion_rules(input sim_pkg::neighbourhood_t stencil,
                                                 input logic can_move_free,
                                                 input logic diagonal_cheap);
    return explosion_rules_b(explosion_rules_a(stencil, can_move_free, diagonal_cheap));
  endfunction

  function automatic sim_pkg::pixel_t gol_rules(input sim_pkg::neighbourhood_t stencil,
                                           input int counter, input logic refresh,
                                           input logic refresh_state);
    logic [3:0] neighbour_count =
        4'(stencil.top.material         == sim_pkg::MatGOL && stencil.top.data[3]) +
        4'(stencil.bottom.material      == sim_pkg::MatGOL && stencil.bottom.data[3]) +
        4'(stencil.topLeft.material     == sim_pkg::MatGOL && stencil.topLeft.data[3]) +
        4'(stencil.left.material        == sim_pkg::MatGOL && stencil.left.data[3]) +
        4'(stencil.bottomLeft.material  == sim_pkg::MatGOL && stencil.bottomLeft.data[3]) +
        4'(stencil.topRight.material    == sim_pkg::MatGOL && stencil.topRight.data[3]) +
        4'(stencil.right.material       == sim_pkg::MatGOL && stencil.right.data[3]) +
        4'(stencil.bottomRight.material == sim_pkg::MatGOL && stencil.bottomRight.data[3]);


    logic center_is_alive = stencil.center.material == sim_pkg::MatGOL && stencil.center.data[3];

    // [SYNTH FIX] single result variable, single return
    logic can_live = sim_pkg::mat_solid(
        stencil.center.material
    ) && !sim_pkg::mat_spreads(
        stencil.center.material
    ) && (neighbour_count == 4'd3 || (neighbour_count == 4'd2 && center_is_alive));
    logic [7:0] result;
    result = 8'hFF;  // no change
    if (can_live) result = {sim_pkg::MatGOL, refresh ? refresh_state : 1'b1, 3'b000};
    else if (stencil.center.material == sim_pkg::MatGOL)
      result = {sim_pkg::MatGOL, refresh ? refresh_state : 1'b0, 3'b000};
    return result;
  endfunction

  function automatic sim_pkg::pixel_t stone_generator_rules(input sim_pkg::neighbourhood_t stencil);
    sim_pkg::pixel_t result = 'hFF;
    if (stencil.center.material == sim_pkg::MatLava)
      if (stencil.top.material == sim_pkg::MatWater)
        result = {sim_pkg::MatStone, 1'b0, 3'(sim_pkg::IgneousStoneThickness)};
      else if (stencil.top.material == sim_pkg::MatStone && stencil.top[2:0] > 0)
        result = {sim_pkg::MatStone, 1'b0, stencil.top[2:0] - 3'd1};
    return result;
  endfunction

  // [SYNTH FIX 2] The output-row pixels are passed in as two plain 24-bit values,
  // {right, centre, left}: out_upper from output buffer 1, out_lower from output buffer 0.
  // Was: the whole buffer array passed by `ref` and indexed inside the function, which
  // synthesis did not build the way simulation runs it (nothing ever moved).
  function automatic sim_pkg::gravity_movement_t gravity_rules(
      input sim_pkg::neighbourhood_row_t out_upper, input sim_pkg::neighbourhood_row_t out_middle,
      input sim_pkg::neighbourhood_t stencil, input logic r);

    logic [3:0] center_density = sim_pkg::mat_density(stencil.center.material);

    logic m_n1_n1 = sim_pkg::mat_displaceable(stencil.topLeft.material);
    logic m_n1_0 = sim_pkg::mat_displaceable(stencil.left.material);
    logic m_n1_1 = sim_pkg::mat_displaceable(stencil.bottomLeft.material);
    logic m_0_n1 = sim_pkg::mat_displaceable(stencil.top.material);
    logic m_0_0 = sim_pkg::mat_displaceable(stencil.center.material);
    logic m_0_1 = sim_pkg::mat_displaceable(stencil.bottom.material);
    logic m_1_n1 = sim_pkg::mat_displaceable(stencil.topRight.material);
    logic m_1_0 = sim_pkg::mat_displaceable(stencil.right.material);
    logic m_1_1 = sim_pkg::mat_displaceable(stencil.bottomRight.material);

    logic d_n1_n1 = sim_pkg::mat_density(stencil.topLeft.material) > center_density;
    logic d_0_n1 = sim_pkg::mat_density(stencil.top.material) > center_density;
    logic d_1_n1 = sim_pkg::mat_density(stencil.topRight.material) > center_density;
    logic d_n1_1 = sim_pkg::mat_density(stencil.bottomLeft.material) < center_density;
    logic d_0_1 = sim_pkg::mat_density(stencil.bottom.material) < center_density;
    logic d_1_1 = sim_pkg::mat_density(stencil.bottomRight.material) < center_density;

    logic unchanged_n1_n1 = stencil.topLeft == out_upper.left;
    logic unchanged_0_n1 = stencil.top == out_upper.center;
    logic unchanged_1_n1 = stencil.topRight == out_upper.right;
    logic unchanged_0_0 = stencil.center == out_middle.center;

    logic d_topLeft_lte_left = sim_pkg::mat_density(
        stencil.topLeft.material
    ) <= sim_pkg::mat_density(
        stencil.left.material
    );
    logic d_topRight_lte_right = sim_pkg::mat_density(
        stencil.topRight.material
    ) <= sim_pkg::mat_density(
        stencil.right.material
    );
    logic d_left_lte_bottomLeft = sim_pkg::mat_density(
        stencil.left.material
    ) <= sim_pkg::mat_density(
        stencil.bottomLeft.material
    );
    logic d_right_lte_bottomRight = sim_pkg::mat_density(
        stencil.right.material
    ) <= sim_pkg::mat_density(
        stencil.bottomRight.material
    );

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
      input sim_pkg::neighbourhood_row_t out_upper, input sim_pkg::neighbourhood_row_t out_middle,
      input sim_pkg::neighbourhood_t stencil, input logic r, input logic is_sending_row,
      input logic at_left_edge, input logic at_right_edge);
    // Relies on the fact that every gravity rule has been evaluated prior.

    logic a_0_n1 = stencil.top.material == sim_pkg::MatAir;
    logic a_n1_n1 = stencil.topLeft.material == sim_pkg::MatAir;
    logic a_1_n1 = stencil.topRight.material == sim_pkg::MatAir;
    logic a_0_0 = stencil.center.material == sim_pkg::MatAir;
    logic a_n1_0 = stencil.left.material == sim_pkg::MatAir;
    logic a_1_0 = stencil.right.material == sim_pkg::MatAir;

    logic s_0_n1 = sim_pkg::mat_spreads(stencil.top.material);
    logic s_0_0 = sim_pkg::mat_spreads(stencil.center.material);

    logic top_can_fall = sim_pkg::mat_displaceable(
        stencil.center.material
    ) && (sim_pkg::mat_falls(
        stencil.top.material
    ) && (sim_pkg::mat_density(
        stencil.top.material
    ) > sim_pkg::mat_density(
        stencil.center.material
    )) || sim_pkg::mat_falls(
        stencil.topLeft.material
    ) && (sim_pkg::mat_density(
        stencil.topLeft.material
    ) > sim_pkg::mat_density(
        stencil.center.material
    )) || sim_pkg::mat_falls(
        stencil.topRight.material
    ) && (sim_pkg::mat_density(
        stencil.topRight.material
    ) > sim_pkg::mat_density(
        stencil.center.material
    )));

    logic center_can_fall = sim_pkg::mat_displaceable(
        stencil.bottom.material
    ) && (sim_pkg::mat_falls(
        stencil.center.material
    ) && (sim_pkg::mat_density(
        stencil.center.material
    ) > sim_pkg::mat_density(
        stencil.bottom.material
    )) || sim_pkg::mat_falls(
        stencil.left.material
    ) && (sim_pkg::mat_density(
        stencil.left.material
    ) > sim_pkg::mat_density(
        stencil.bottom.material
    )) || sim_pkg::mat_falls(
        stencil.right.material
    ) && (sim_pkg::mat_density(
        stencil.right.material
    ) > sim_pkg::mat_density(
        stencil.bottom.material
    )));

    sim_pkg::fluid_movement_t res = 'b0000;



    logic top_unchanged = stencil.top == out_upper.center;
    logic top_left_unchanged = 0, top_right_unchanged = 0;

    logic center_unchanged = stencil.center == out_middle.center;
    logic left_unchanged = 0, right_unchanged = 0;

    if (!at_left_edge) begin
      top_left_unchanged = stencil.topLeft == out_upper.left;

      left_unchanged = stencil.left == out_middle.left;
    end

    if (!at_right_edge) begin
      top_right_unchanged = stencil.topRight == out_upper.right;

      right_unchanged = stencil.right == out_middle.right;
    end

    // If we're a not a sending row, we know our sending row can't
    // issue a transaction. So we can evaluate both rows safely.
    // [SYNTH FIX] was an early "return res;" for sending rows; now gated, single return.

    // Top row
    if (!is_sending_row && !top_can_fall && !a_0_n1 && s_0_n1) begin
      // Incoming
      if (r && !at_right_edge && a_1_n1 && top_unchanged && top_right_unchanged) res.topRight = 1;
      else if (!r && !at_left_edge && a_n1_n1 && top_unchanged && top_left_unchanged)
        res.topLeft = 1;
    end

    // Bottom row
    if (!is_sending_row && !center_can_fall && !a_0_0 && s_0_0) begin
      // Incoming
      if (r && !at_right_edge && a_1_0 && center_unchanged && right_unchanged) res.centerRight = 1;
      else if (!r && !at_left_edge && a_n1_0 && center_unchanged && left_unchanged)
        res.centerLeft = 1;
    end

    return res;

  endfunction
endpackage
