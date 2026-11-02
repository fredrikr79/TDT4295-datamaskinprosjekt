module simulator #(

)(
    input wire clk, pulse_new_row, sim_running,
    input sim_pkg::row_buffer_t buffers [3],
    output logic pulse_transaction_pair_done, pulse_row_done,
    output sim_pkg::row_buffer_t res_buffer [2]
);

import sim_pkg::*;

integer base = 0;
integer i;
logic row_running = 1'b0;
logic [15:0] stride = sim_pkg::CellGridWidth / sim_pkg::SimulationCoreCount;
logic [3:0] sim_clock = 4'b0;

logic [15:0] row_index = 16'd0;
logic odd_major_evaluation = 1'b0;

// Linear feedback shift register acting as a L/R decision selector.
logic [31:0] lfsr = 32'hACE1_2345;

// Write transaction sends to res_buffer 0 and recieves to 1
logic sending_row;
assign sending_row = row_index[0] == odd_major_evaluation;

logic transaction_unmatched_row;
assign transaction_unmatched_row = odd_major_evaluation && (row_index == 0 || row_index == sim_pkg::CellGridHeight - 1);

// Find pixel neighbourhood in parallell:
always @(posedge clk) begin

    pulse_transaction_pair_done <= 1'b0;
    pulse_row_done <= 1'b0;

    if (pulse_new_row && sim_running) begin
        if (transaction_unmatched_row) begin
            res_buffer[sending_row] <= buffers[1];
        end
        else if (sending_row) begin
            // First row of a transaction pair
            res_buffer[1] <= buffers[1];
            res_buffer[0] <= buffers[2];
        end

        row_running <= 1'b1;
    end

    for(i = 0; i < sim_pkg::SimulationCoreCount; i = i + 1) begin : process_lane

        sim_pkg::neighbourhood_t neighbourhood;
        logic [3:0] gravity_mat = 4'hF, fluid_mat = 4'hF; // uses a case handler to swap materials.
        logic [7:0] explo_mat, gol_mat; // returns material directly


        // i * stride = base addr for sim core. base = round offset.
        automatic integer x;
        automatic integer curr;
        automatic integer left;
        automatic integer right;

        x     = base + i * stride;
        curr  = x * sim_pkg::BitsPerPixel;
        left  = (x - 1) * sim_pkg::BitsPerPixel;
        right = (x + 1) * sim_pkg::BitsPerPixel;

        if(!row_running) begin
            continue;
        end

        neighbourhood = '{default: {MatAir, 4'b0}};

        // Center always exists.
        neighbourhood.center =
            buffers[1][curr +: BitsPerPixel];

        // Upper row.
        if (row_index > 0) begin
            neighbourhood.top =
                buffers[0][curr +: BitsPerPixel];

            if (x > 0)
                neighbourhood.topLeft =
                    buffers[0][left +: BitsPerPixel];

            if (x < CellGridWidth-1)
                neighbourhood.topRight =
                    buffers[0][right +: BitsPerPixel];
        end

        // Lower row.
        if (row_index < CellGridHeight-1) begin
            neighbourhood.bottom =
                buffers[2][curr +: BitsPerPixel];

            if (x > 0)
                neighbourhood.bottomLeft =
                    buffers[2][left +: BitsPerPixel];

            if (x < CellGridWidth-1)
                neighbourhood.bottomRight =
                    buffers[2][right +: BitsPerPixel];
        end

        // Horizontal neighbours.
        if (x > 0)
            neighbourhood.left =
                buffers[1][left +: BitsPerPixel];

        if (x < CellGridWidth-1)
            neighbourhood.right =
                buffers[1][right +: BitsPerPixel];

        gravity_mat = 4'hF;
        fluid_mat   = 4'hF;

        // Priority based situation resolution. Evaluation is done in parallell
        // To avoid race conditions each frame will only translate pixels in
        // Either the left or right direction (in addition to vertical)
        explo_mat = explosion_rules(neighbourhood, sim_clock);
        gol_mat = gol_rules(neighbourhood, sim_clock);

        // Only eval transactions on recieving row of a transaction pair
        if(!sending_row && !transaction_unmatched_row) begin
            gravity_mat = gravity_rules(neighbourhood, lfsr[0], curr, sim_clock);
            fluid_mat = fluid_rules(neighbourhood, lfsr[0], x, curr, sim_clock);
        end

        // Write res_buffers

        // Update Rules
        if (explo_mat != 8'hFF) begin
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= explo_mat;
        end else if (gol_mat != 8'hFF) begin
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= gol_mat;
        end else if (gravity_mat != 4'hF) begin
            if(gravity_mat == 4'd0) begin
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
                res_buffer[!sending_row][curr - sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.center;
            end else if(gravity_mat == 4'd1) begin
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.center;
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
            end else if(gravity_mat == 4'd2) begin
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topRight;
                res_buffer[!sending_row][curr + sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.center;
            end
        end else if(fluid_mat != 4'hF) begin
            if (fluid_mat[3:2] == 2'd0) begin
                res_buffer[!sending_row][curr - sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.top;
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
            end else if (fluid_mat[3:2] == 2'd2) begin
                res_buffer[!sending_row][curr + sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.top;
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topRight;
            end

            if (fluid_mat[1:0] == 2'd0) begin
                res_buffer[sending_row][curr - sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.center;
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.left;
            end else if (fluid_mat[1:0] == 2'd2) begin
                res_buffer[sending_row][curr + sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel]
                    <= neighbourhood.center;
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.right;
            end
        end


    end
    // advancement logic
    if(row_running) begin
        if(base == stride - 1) begin
            base <= 0;
            row_running <= 1'b0;
            pulse_row_done <= 1'b1;

            if(!sending_row || transaction_unmatched_row) begin
                pulse_transaction_pair_done <= 1'b1;
                lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
            end

            if(row_index == sim_pkg::CellGridHeight - 1) begin
                row_index <= 0;
                sim_clock <= sim_clock + 1'b1;
                odd_major_evaluation <= ~odd_major_evaluation;
            end else begin
                row_index <= row_index + 1'b1;
            end
        end else begin
            // Update base index
            base <= base + 32'd1;
        end
    end
end

function automatic logic [7:0] explosion_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic [3:0] sim_clock
);

    automatic integer full_force;

    automatic integer p1 =
        (stencil.topLeft[7:4]     == sim_pkg::MatExplo ? stencil.topLeft[3:0]     : 4'd0) -
        (stencil.bottomRight[7:4] == sim_pkg::MatExplo ? stencil.bottomRight[3:0] : 4'd0);
    automatic integer p2 =
        (stencil.top[7:4]         == sim_pkg::MatExplo ? stencil.top[3:0]         : 4'd0)-
        (stencil.bottom[7:4]      == sim_pkg::MatExplo ? stencil.bottom[3:0]      : 4'd0);
    automatic integer p3 =
        (stencil.topRight[7:4]    == sim_pkg::MatExplo ? stencil.topRight[3:0]    : 4'd0)-
        (stencil.bottomLeft[7:4]  == sim_pkg::MatExplo ? stencil.bottomLeft[3:0]  : 4'd0);
    automatic integer p4 =
        (stencil.right[7:4]       == sim_pkg::MatExplo ? stencil.right[3:0]       : 4'd0)-
        (stencil.left[7:4]        == sim_pkg::MatExplo ? stencil.left[3:0]        : 4'd0);
    automatic integer center =
        (stencil.center[7:4]      == sim_pkg::MatExplo ? stencil.center[3:0]      : 4'd0);

    automatic integer blast_resistance = props(stencil.center[7:4]).blast_resistance;

    logic is_exploding =
        stencil.topLeft[7:4]     == sim_pkg::MatExplo ||
        stencil.top[7:4]         == sim_pkg::MatExplo ||
        stencil.topRight[7:4]    == sim_pkg::MatExplo ||
        stencil.left[7:4]        == sim_pkg::MatExplo ||
        stencil.center[7:4]      == sim_pkg::MatExplo ||
        stencil.right[7:4]       == sim_pkg::MatExplo ||
        stencil.bottomLeft[7:4]  == sim_pkg::MatExplo ||
        stencil.bottom[7:4]      == sim_pkg::MatExplo ||
        stencil.bottomRight[7:4] == sim_pkg::MatExplo;

    logic [3:0] force_strength = 8'b0;
    // Abs value
    if (p1 < 0) begin
        p1 = -p1;
    end
    if (p2 < 0) begin
        p2 = -p2;
    end
    if (p3 < 0) begin
        p3 = -p3;
    end
    if (p4 < 0) begin
        p4 = -p4;
    end

    full_force = p1 + p2 + p3 + p4 + center - blast_resistance;

    if(is_exploding && full_force > 0 && sim_clock % props(sim_pkg::MatExplo).delay == 4'b0) begin
        force_strength = full_force > 4'd15 ? 4'hF : full_force;
        return {sim_pkg::MatExplo, force_strength};
    end else return 8'hFF;
endfunction

function automatic logic [7:0] gol_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic [3:0] sim_clock
);
    automatic logic [3:0] neighbour_count =
        (stencil.top[7:4]         == sim_pkg::MatGOL) +
        (stencil.bottom[7:4]      == sim_pkg::MatGOL) +
        (stencil.topLeft[7:4]     == sim_pkg::MatGOL) +
        (stencil.left[7:4]        == sim_pkg::MatGOL) +
        (stencil.bottomLeft[7:4]  == sim_pkg::MatGOL) +
        (stencil.topRight[7:4]    == sim_pkg::MatGOL) +
        (stencil.right[7:4]       == sim_pkg::MatGOL) +
        (stencil.bottomRight[7:4] == sim_pkg::MatGOL);

    if(props(stencil.center[7:4]).solid &&
      !props(stencil.center[7:4]).displaceable &&
      sim_clock % props(sim_pkg::MatGOL).delay == 4'b0
    ) begin
        return (neighbour_count + (stencil.center[7:4] == sim_pkg::MatGOL)) == 4'd3
            ? {MatGOL , 4'b0}
            : 8'hFF;
    end
    return 8'hFF;
endfunction

function automatic logic [3:0] gravity_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic r,
    input logic [15:0] curr,
    input logic[3:0] sim_clock
);

    logic [3:0] center_density = props(stencil.center[7:4]).density;

    logic c_0_0   = sim_clock % props(stencil.center[7:4]).delay == 4'd0;
    logic c_n1_n1 = sim_clock % props(stencil.topLeft[7:4]).delay == 4'd0;
    logic c_0_n1  = sim_clock % props(stencil.top[7:4]).delay == 4'd0;
    logic c_1_n1  = sim_clock % props(stencil.topRight[7:4]).delay == 4'd0;

    logic m_n1_n1 = props(stencil.topLeft[7:4]).displaceable;
    logic m_n1_0  = props(stencil.left[7:4]).displaceable;
    logic m_n1_1  = props(stencil.bottomLeft[7:4]).displaceable;
    logic m_0_n1  = props(stencil.top[7:4]).displaceable;
    logic m_0_0   = props(stencil.center[7:4]).displaceable;
    logic m_0_1   = props(stencil.bottom[7:4]).displaceable;
    logic m_1_n1  = props(stencil.topRight[7:4]).displaceable;
    logic m_1_0   = props(stencil.right[7:4]).displaceable;
    logic m_1_1   = props(stencil.bottomRight[7:4]).displaceable;

    logic d_n1_n1 = props(stencil.topLeft[7:4]).density > center_density;
    logic d_0_n1  = props(stencil.top[7:4]).density > center_density;
    logic d_1_n1  = props(stencil.topRight[7:4]).density > center_density;
    logic d_n1_1  = props(stencil.bottomLeft[7:4]).density < center_density;
    logic d_0_1   = props(stencil.bottom[7:4]).density < center_density;
    logic d_1_1   = props(stencil.bottomRight[7:4]).density < center_density;

    logic unchanged_n1_n1 =
        stencil.topLeft == res_buffer[1][curr - sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel];
    logic unchanged_0_n1 =
        stencil.top == res_buffer[1][curr +: sim_pkg::BitsPerPixel];
    logic unchanged_1_n1 =
        stencil.topRight == res_buffer[1][curr + sim_pkg::BitsPerPixel +: sim_pkg::BitsPerPixel];
    logic unchanged_0_0   =
        stencil.center == res_buffer[0][curr +: sim_pkg::BitsPerPixel];

    logic d_topLeft_lte_left = props(stencil.topLeft[7:4]).density
        <= props(stencil.left[7:4]).density;
    logic d_topRight_lte_right = props(stencil.topRight[7:4]).density
        <= props(stencil.right[7:4]).density;
    logic d_left_lte_bottomLeft = props(stencil.left[7:4]).density
        <= props(stencil.bottomLeft[7:4]).density;
    logic d_right_lte_bottomRight = props(stencil.right[7:4]).density
        <= props(stencil.bottomRight[7:4]).density;

    logic moveDown = unchanged_0_n1 && unchanged_0_0 &&  m_0_n1 && d_0_n1 && c_0_n1;

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

    if(m_0_0) begin // Top pixel decides movement timings
        if (moveDown) begin
            return 4'd1;
        end else if (moveRight) begin
            return 4'd0;
        end else if (moveLeft) begin
            return 4'd2;
        end else return 4'hF;
    end else return 4'hF;
endfunction

function automatic logic [3:0] fluid_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic r,
    input integer x,
    input integer curr,
    input logic [3:0] sim_clock
);
    // Relies on the fact that every gravity rule has been evaluated prior.
    logic c_0_n1  = sim_clock % props(stencil.top[7:4]).delay == 4'd0;
    logic c_0_0   = sim_clock % props(stencil.center[7:4]).delay == 4'd0;

    logic a_0_n1 = stencil.top[7:4]       == sim_pkg::MatAir;
    logic a_n1_n1 = stencil.topLeft[7:4]  == sim_pkg::MatAir;
    logic a_1_n1 = stencil.topRight[7:4]  == sim_pkg::MatAir;
    logic a_0_0 = stencil.center[7:4]     == sim_pkg::MatAir;
    logic a_n1_0 = stencil.left[7:4]      == sim_pkg::MatAir;
    logic a_1_0 = stencil.right[7:4]      == sim_pkg::MatAir;

    logic s_0_n1 = props(stencil.top[7:4]).spreads;
    logic s_0_0 = props(stencil.center[7:4]).spreads;

    logic top_can_fall =
        props(stencil.center[7:4]).displaceable &&
        (
            props(stencil.top[7:4]).falls &&
            (props(stencil.top[7:4]).density >
            props(stencil.center[7:4]).density) ||

            props(stencil.topLeft[7:4]).falls &&
            (props(stencil.topLeft[7:4]).density >
            props(stencil.center[7:4]).density) ||

            props(stencil.topRight[7:4]).falls &&
            (props(stencil.topRight[7:4]).density >
            props(stencil.center[7:4]).density)
        );

    logic center_can_fall =
        props(stencil.bottom[7:4]).displaceable &&
        (
            props(stencil.center[7:4]).falls &&
            (props(stencil.center[7:4]).density >
            props(stencil.bottom[7:4]).density) ||

            props(stencil.left[7:4]).falls &&
            (props(stencil.left[7:4]).density >
            props(stencil.bottom[7:4]).density) ||

            props(stencil.right[7:4]).falls &&
            (props(stencil.right[7:4]).density >
            props(stencil.bottom[7:4]).density)
        );

    logic [3:0] res = 4'hF;

    // Upper row: output buffer 1
    logic top_unchanged;
    logic top_left_unchanged, top_right_unchanged;

    // Lower row: output buffer 0
    logic center_unchanged;
    logic left_unchanged, right_unchanged;

    top_unchanged =
        stencil.top == res_buffer[1][curr +: BitsPerPixel];

    center_unchanged =
        stencil.center == res_buffer[0][curr +: BitsPerPixel];

    top_left_unchanged = 1'b0;
    top_right_unchanged = 1'b0;
    left_unchanged = 1'b0;
    right_unchanged = 1'b0;

    if (x > 0) begin
        top_left_unchanged =
            stencil.topLeft == res_buffer[1][curr-BitsPerPixel +: BitsPerPixel];

        left_unchanged =
            stencil.left == res_buffer[0][curr-BitsPerPixel +: BitsPerPixel];
    end

    if (x < CellGridWidth-1) begin
        top_right_unchanged =
            stencil.topRight == res_buffer[1][curr+BitsPerPixel +: BitsPerPixel];

        right_unchanged =
            stencil.right == res_buffer[0][curr+BitsPerPixel +: BitsPerPixel];
    end

    // If we're a not a sending row, we know our sending row can't
    // issue a transaction. So we can evaluate both rows safely.

    if (sending_row) begin
        return 4'hF;
    end

    // Top row
    if (!top_can_fall && c_0_n1 && !a_0_n1 && s_0_n1) begin
        // Incoming
        if(r &&
            x < CellGridWidth-1 &&
            a_1_n1 &&
            top_unchanged &&
            top_right_unchanged
        ) begin
            res[3:2] = 2'd2;
        end else if(!r &&
            x > 0 &&
            a_n1_n1 &&
            top_unchanged &&
            top_left_unchanged
        ) begin
            res[3:2] = 2'd0;
        end
    end

    // Bottom row
    if (!center_can_fall && c_0_0 && !a_0_0 && s_0_0) begin
        // Incoming
        if(r &&
            x < CellGridWidth-1 &&
            a_1_0 &&
            center_unchanged &&
            right_unchanged
        ) begin
            res[1:0] = 2'd2;
        end else if(!r &&
            x > 0 &&
            a_n1_0 &&
            center_unchanged &&
            left_unchanged
        ) begin
            res[1:0] = 2'd0;
        end
    end

    return res;

endfunction


endmodule


