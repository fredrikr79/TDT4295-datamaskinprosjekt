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
  int   simulation_base = 0;
  int   simulation_core_index;
  logic is_row_running = 0;
  int   simulation_stride = sim_pkg::CellGridWidth / sim_pkg::SimulationCoreCount;
  int   simulation_counter = 0;
  int   gol_refresh_counter = 1;

  int   current_row_index = 0;
  logic is_odd_major_evaluation = 0;

  // Linear feedback shift register acting as a L/R decision selector.
  int   lfsr = 32'hACE1_2345;
  int   lfsr_gol = 32'hACE1_2345;

  // Write transaction sends to res_buffer 0 and recieves to 1
  logic is_sending_row;
  assign is_sending_row = current_row_index[0] == is_odd_major_evaluation;

  logic is_unmatched_boundry_row;
  // calculation here only works for even CellGridHeights
  assign is_unmatched_boundry_row = is_odd_major_evaluation &&
    (current_row_index == 0 || current_row_index == sim_pkg::CellGridHeight - 1);

  always @(posedge clk) begin
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

    // advancement logic
    if (is_row_running) begin
      if (simulation_base == simulation_stride - 1) begin
        simulation_base <= 0;
        is_row_running  <= 1'b0;
        pulse_row_done  <= 1'b1;

        if (!is_sending_row || is_unmatched_boundry_row) begin
          pulse_transaction_pair_done <= 1'b1;
          lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        end

        if (current_row_index == sim_pkg::CellGridHeight - 1) begin
          current_row_index <= 0;
          simulation_counter <= simulation_counter + 1'b1;

          gol_refresh_counter <= gol_refresh_counter + 1'b1;
          lfsr_gol <= {lfsr_gol[30:0], lfsr_gol[31] ^ lfsr_gol[21] ^ lfsr_gol[1] ^ lfsr_gol[0]};

          is_odd_major_evaluation <= ~is_odd_major_evaluation;
        end else begin
          current_row_index <= current_row_index + 1'b1;
        end
      end else begin
        // Update base index
        simulation_base <= simulation_base + 32'd1;
      end
    end
  end

  always @(posedge clk) begin
    for (
        simulation_core_index = 0;
        simulation_core_index < sim_pkg::SimulationCoreCount;
        simulation_core_index = simulation_core_index + 1
    ) begin : process_lane


      // i * stride = base addr for sim core. base = round offset.
      automatic
      int
      current_pixel_column = simulation_base + simulation_core_index * simulation_stride;
      automatic sim_pkg::neighbourhood_t neighbourhood = setup_neighbourhood(current_pixel_column);
      automatic int current_pixel_bit_column = current_pixel_column * sim_pkg::BitsPerPixel;
      automatic int left_pixel_bit_column = (current_pixel_column - 1) * sim_pkg::BitsPerPixel;
      automatic int right_pixel_bit_column = (current_pixel_column + 1) * sim_pkg::BitsPerPixel;

      automatic
      logic [7:0]
      explosion_update = sim_rules::explosion_rules(
          neighbourhood, current_pixel_column, current_row_index, simulation_counter
      );
      automatic
      logic [7:0]
      gol_update = sim_rules::gol_rules(
          neighbourhood,
          simulation_counter,
          gol_refresh_counter % sim_pkg::GolRefreshDelay == 0,
          sim_pkg::lowbias32(
              lfsr_gol ^ current_pixel_bit_column) [0]
      );

      automatic sim_pkg::gravity_movement_t gravity_movement = sim_pkg::DownNone;
      automatic sim_pkg::fluid_movement_t fluid_movement = 'b0000;
      automatic logic [7:0] stone_generator_update = 'hFF;
      if (!is_sending_row && !is_unmatched_boundry_row) begin
        gravity_movement = sim_rules::gravity_rules(output_buffers, neighbourhood, lfsr[0],
                                                    current_pixel_bit_column, simulation_counter);
        fluid_movement = sim_rules::fluid_rules(
          output_buffers,
          neighbourhood,
          lfsr[0],
          current_pixel_column,
          simulation_counter,
          is_sending_row
        );

        stone_generator_update =
            sim_rules::stone_generator_rules(neighbourhood, simulation_counter);
      end

      // Update Rules
      if (!is_row_running) continue;
      if (explosion_update != 8'hFF)
        output_buffers[is_sending_row][current_pixel_bit_column+:sim_pkg::BitsPerPixel]
            <= explosion_update;
      else if (gol_update != 8'hFF)
        output_buffers[is_sending_row][current_pixel_bit_column+:sim_pkg::BitsPerPixel]
            <= gol_update;
      else if (stone_generator_update != 8'hFF)
        output_buffers[is_sending_row][current_pixel_bit_column +: sim_pkg::BitsPerPixel]
            <= stone_generator_update;
      else if (gravity_movement != sim_pkg::DownNone)
        apply_gravity_movement(gravity_movement, neighbourhood, current_pixel_column);
      else if (!sim_pkg::no_movement(fluid_movement))
        apply_fluid_movement(fluid_movement, neighbourhood, current_pixel_column);
    end

  end

  function automatic sim_pkg::neighbourhood_t setup_neighbourhood(int column);
    int center_bit_column = column * sim_pkg::BitsPerPixel;
    int left_bit_column = (column - 1) * sim_pkg::BitsPerPixel;
    int right_bit_column = (column + 1) * sim_pkg::BitsPerPixel;
    sim_pkg::neighbourhood_t neighbourhood = '{default: {sim_pkg::MatAir, 4'b0}};
    // Center always exists.
    neighbourhood.center = input_buffers[1][center_bit_column+:sim_pkg::BitsPerPixel];

    // Upper row.
    if (current_row_index > 0) begin
      neighbourhood.top = input_buffers[0][center_bit_column+:sim_pkg::BitsPerPixel];

      if (column > 0)
        neighbourhood.topLeft = input_buffers[0][left_bit_column+:sim_pkg::BitsPerPixel];

      if (column < sim_pkg::CellGridWidth - 1)
        neighbourhood.topRight = input_buffers[0][right_bit_column+:sim_pkg::BitsPerPixel];
    end

    // Lower row.
    if (current_row_index < sim_pkg::CellGridHeight - 1) begin
      neighbourhood.bottom = input_buffers[2][center_bit_column+:sim_pkg::BitsPerPixel];

      if (column > 0)
        neighbourhood.bottomLeft = input_buffers[2][left_bit_column+:sim_pkg::BitsPerPixel];

      if (column < sim_pkg::CellGridWidth - 1)
        neighbourhood.bottomRight = input_buffers[2][right_bit_column+:sim_pkg::BitsPerPixel];
    end

    // Horizontal neighbours.
    if (column > 0) neighbourhood.left = input_buffers[1][left_bit_column+:sim_pkg::BitsPerPixel];

    if (column < sim_pkg::CellGridWidth - 1)
      neighbourhood.right = input_buffers[1][right_bit_column+:sim_pkg::BitsPerPixel];

    return neighbourhood;
  endfunction


  task automatic apply_gravity_movement(sim_pkg::gravity_movement_t gravity_movement,
                                        sim_pkg::neighbourhood_t neighbourhood, int pixel_column);
    int center_bit_column = pixel_column * sim_pkg::BitsPerPixel;
    int left_bit_column = (pixel_column - 1) * sim_pkg::BitsPerPixel;
    int right_bit_column = (pixel_column + 1) * sim_pkg::BitsPerPixel;
    if (gravity_movement == sim_pkg::DownRight) begin
      output_buffers[is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
            <= neighbourhood.topLeft;

      output_buffers[!is_sending_row][left_bit_column +: sim_pkg::BitsPerPixel]
            <= neighbourhood.center;

    end else if (gravity_movement == sim_pkg::Down) begin
      output_buffers[!is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
            <= neighbourhood.center;

      output_buffers[is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel] <= neighbourhood.top;

    end else if (gravity_movement == sim_pkg::DownLeft) begin
      output_buffers[is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
            <= neighbourhood.topRight;

      output_buffers[!is_sending_row][right_bit_column +: sim_pkg::BitsPerPixel]
            <= neighbourhood.center;
    end
  endtask

  task automatic apply_fluid_movement(sim_pkg::fluid_movement_t fluid_movement,
                                      sim_pkg::neighbourhood_t neighbourhood, int pixel_column);
    int center_bit_column = pixel_column * sim_pkg::BitsPerPixel;
    int left_bit_column = (pixel_column - 1) * sim_pkg::BitsPerPixel;
    int right_bit_column = (pixel_column + 1) * sim_pkg::BitsPerPixel;

    if (fluid_movement.topLeft) begin
      output_buffers[!is_sending_row][left_bit_column+:sim_pkg::BitsPerPixel] <= neighbourhood.top;

      output_buffers[!is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
        <= neighbourhood.topLeft;

    end else if (fluid_movement.topRight) begin
      output_buffers[!is_sending_row][right_bit_column+:sim_pkg::BitsPerPixel] <= neighbourhood.top;

      output_buffers[!is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
        <= neighbourhood.topRight;

    end
    if (fluid_movement.centerLeft) begin
      output_buffers[is_sending_row][left_bit_column +: sim_pkg::BitsPerPixel]
        <= neighbourhood.center;

      output_buffers[is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
        <= neighbourhood.left;

    end else if (fluid_movement.centerRight) begin
      output_buffers[is_sending_row][right_bit_column +: sim_pkg::BitsPerPixel]
        <= neighbourhood.center;

      output_buffers[is_sending_row][center_bit_column+:sim_pkg::BitsPerPixel]
        <= neighbourhood.right;

    end
  endtask



endmodule
