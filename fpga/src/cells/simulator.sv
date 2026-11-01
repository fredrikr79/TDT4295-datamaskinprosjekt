module simulator #(

)(
    input wire clk, pulse_new_row, sim_running, frame_toggle,
    input sim_pkg::row_buffer_t buffers [3],
    output logic pulse_transaction_pair_done,
    output sim_pkg::row_buffer_t res_buffer [2]
);

import sim_pkg::*;

integer base = 0;
integer i;
logic row_running = 1'b0;
logic [15:0] stride = sim_pkg::CellGridWidth / sim_pkg::SimulationCoreCount;
logic [3:0] sim_clock = 4'b0;

logic [15:0] row_index = 16'd0;
logic even_major_evaluation = 1'b0;

// Linear feedback shift register acting as a L/R decision selector.
logic [31:0] lfsr = 32'hACE1_2345;

// Write transaction sends to res_buffer 0 and recieves to 1
logic sending_row;
assign sending_row = row_index[0] != even_major_evaluation;



// Find pixel neighbourhood in parallell:
always @(posedge clk) begin

    if(frame_toggle) begin
        // Swap sending and recieving buffers
        even_major_evaluation = ~even_major_evaluation;
    end

    if(pulse_new_row && sim_running) begin
        row_running <= 1'b1;
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        row_index <= 16'd0;
        sim_clock <= sim_clock + 1'b1;
    end

    for(i = sim_pkg::SimulationCoreCount-1; i >= 0; i = i + 1) begin : process_lane

        sim_pkg::neighbourhood_t neighbourhood;
        logic [4:0] gravity_mat, fluid_mat; // uses a case handler to swap materials.
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

        if(x == sim_pkg::CellGridWidth - 1) begin
            base <= 0;
            row_running <= 1'b0;
            even_major_evaluation <= ~even_major_evaluation;
            row_index <= row_index + 1'b1;
            if(!sending_row)
                pulse_transaction_pair_done <= 1'b1;
        end

        neighbourhood = '{default: MatAir};
            neighbourhood.top         = buffers[0][curr +: sim_pkg::BitsPerPixel];
            neighbourhood.center      = buffers[1][curr +: sim_pkg::BitsPerPixel];
            neighbourhood.bottom      = buffers[2][curr +: sim_pkg::BitsPerPixel];

        if(x > 0) begin
            neighbourhood.topLeft     = buffers[0][left +: sim_pkg::BitsPerPixel];
            neighbourhood.left        = buffers[1][left +: sim_pkg::BitsPerPixel];
            neighbourhood.bottomLeft  = buffers[2][left +: sim_pkg::BitsPerPixel];
        end

        if(x < sim_pkg::CellGridWidth - 1) begin
            neighbourhood.topRight    = buffers[0][right +: sim_pkg::BitsPerPixel];
            neighbourhood.right       = buffers[1][right +: sim_pkg::BitsPerPixel];
            neighbourhood.bottomRight = buffers[2][right +: sim_pkg::BitsPerPixel];
        end

        // Priority based situation resolution. Evaluation is done in parallell
        // To avoid race conditions each frame will only translate pixels in
        // Either the left or right direction (in addition to vertical)
        explo_mat = explosion_rules(neighbourhood, sim_clock);
        gol_mat = gol_rules(neighbourhood, sim_clock);

        // Only eval transactions on recieving row of a transaction pair
        if(sending_row) begin
            gravity_mat = gravity_rules(neighbourhood, lfsr[0], curr, sim_clock);
            fluid_mat = fluid_rules(neighbourhood, lfsr[0], sim_clock);
        end

        // Write res_buffers
        if (explo_mat != 8'hFF) begin
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= explo_mat;
        end else if (gol_mat != 8'hFF) begin
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= gol_mat;
        end else if (gravity_mat != 8'hFF) begin
            if(gravity_mat == 4'd0) begin
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
                res_buffer[sending_row][curr - 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.center;
            end else if(gravity_mat == 4'd1) begin
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
                res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.center;
            end else if(gravity_mat == 4'd2) begin
                res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topRight;
                res_buffer[sending_row][curr + 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.center;
            end
        end else if (fluid_mat[3:2] == 2'd0) begin
            res_buffer[sending_row][curr - 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
        end else if (fluid_mat[3:2] == 2'd2) begin
            res_buffer[sending_row][curr + 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
            res_buffer[sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
        end else if (fluid_mat[1:0] == 2'd0) begin
            res_buffer[!sending_row][curr - 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
            res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
        end else if (fluid_mat[1:0] == 2'd2) begin
            res_buffer[!sending_row][curr + 1 +: sim_pkg::BitsPerPixel] <= neighbourhood.top;
            res_buffer[!sending_row][curr +: sim_pkg::BitsPerPixel] <= neighbourhood.topLeft;
        end
        // Update base index
        base <= base + 16'd1;
    end
end

function automatic logic [7:0] explosion_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic [3:0] sim_clock
);

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

    force_strength = p1 + p2 + p3 + p4 + center - blast_resistance;
    if(stencil.center)

    return force_strength > 0 && sim_clock % props(sim_pkg::MatExplo).delay == 4'b0
        ? {sim_pkg::MatExplo, force_strength}
        : 8'hFF;
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

    if(props(stencil.center[7:4]).solid && sim_clock % props(sim_pkg::MatGOL).delay == 4'b0) begin
        return (neighbour_count | (stencil.center[7:4] == sim_pkg::MatGOL)) == 4'd3
            ? {MatGOL , 4'b0}
            : 8'hFF;
    end
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

    logic unchanged_n1_n1 = stencil.topLeft == res_buffer[0][curr - 1];
    logic unchanged_0_n1 = stencil.top == res_buffer[0][curr];
    logic unchanged_1_n1 = stencil.topRight == res_buffer[0][curr + 1];
    logic unchanged_0_0   = stencil.center == res_buffer[1][curr];

    logic d_topLeft_lte_left = props(stencil.topLeft[7:4]).density
        <= props(stencil.left[7:4]).density;
    logic d_topRight_lte_right = props(stencil.topRight[7:4]).density
        <= props(stencil.right[7:4]).density;
    logic d_left_lte_bottomLeft = props(stencil.left[7:4]).density
        <= props(stencil.bottomLeft[7:4]).density;
    logic d_right_lte_bottomRight = props(stencil.right[7:4]).density
        <= props(stencil.bottomRight[7:4]).density;

    logic moveDown = unchanged_0_n1 && m_0_n1 && d_0_n1 && c_0_n1;

    logic moveRight = r &&
        unchanged_n1_n1 &&
        m_n1_n1 &&
        d_n1_n1 &&
        (!m_n1_0 || d_topLeft_lte_left) &&
        c_n1_n1;

    logic moveLeft = !r &&
        unchanged_1_n1 &&
        m_1_n1 &&
        d_1_n1 &&
        (!m_1_0 || d_topRight_lte_right) &&
        c_1_n1;

    if(m_0_0 && sending_row) begin // Top pixel decides movement timings
        if (moveDown) begin
            return 4'd1;
        end else if (moveRight) begin
            return 4'd0;
        end else if (moveLeft) begin
            return 4'd2;
        end else return 4'hF;
    end else return 4'hF;
endfunction

function automatic logic [7:0] fluid_rules(
    input sim_pkg::neighbourhood_t stencil,
    input logic r,
    input logic [3:0] sim_clock
);
    // Relies on the fact that every gravity rule has been evaluated prior.
    logic c_0_n1  = sim_clock % props(stencil.top[7:4]).delay == 4'd0;
    logic c_n1_n1 = sim_clock % props(stencil.topLeft[7:4]).delay == 4'd0;
    logic c_1_n1  = sim_clock % props(stencil.topRight[7:4]).delay == 4'd0;
    logic c_0_0   = sim_clock % props(stencil.center[7:4]).delay == 4'd0;
    logic c_n1_0  = sim_clock % props(stencil.left[7:4]).delay == 4'd0;
    logic c_1_0   = sim_clock % props(stencil.right[7:4]).delay == 4'd0;

    logic a_0_n1 = stencil.top[7:4]       == sim_pkg::MatAir;
    logic a_n1_n1 = stencil.topLeft[7:4]  == sim_pkg::MatAir;
    logic a_1_n1 = stencil.topRight[7:4]  == sim_pkg::MatAir;
    logic a_0_0 = stencil.center[7:4]     == sim_pkg::MatAir;
    logic a_n1_0 = stencil.left[7:4]      == sim_pkg::MatAir;
    logic a_1_0 = stencil.right[7:4]      == sim_pkg::MatAir;

    logic d_0_n1 = props(stencil.top[7:4]).displaceable;
    logic d_0_0  = props(stencil.center[7:4]).displaceable;

    logic [3:0] res;

    // If we're a not a sending row, we know our sending row can't
    // issue a transaction. So we can evaluate both rows safely.

    if (sending_row) begin
        return 8'hFF;
    end

    // Top row
    if (c_0_n1 && !a_0_n1 && d_0_n1) begin
        // Incoming
        if(r && a_1_n1) begin
            res[3:2] = 2'd2;
        end else if(!r && a_n1_n1) begin
            res[3:2] = 2'd0;
        end
    end else if(a_0_n1) begin
        if(r && !a_n1_n1 && c_n1_n1) begin
            res[3:2] = 2'd0;
        end else if(!r && ! a_1_n1 && c_1_n1) begin
            res[3:2] = 2'd2;
        end
    end else res[3:2] = 2'd1;

    // Bottom row
    if (c_0_0 && !a_0_0 && d_0_0) begin
        // Incoming
        if(r && a_1_0) begin
            res[1:0] = 2'd2;
        end else if(!r && a_n1_0) begin
            res[1:0] = 2'd0;
        end
    end else if(a_0_0) begin
        if(r && !a_n1_0 && c_n1_0) begin
            res[1:0] = 2'd0;
        end else if(!r && ! a_1_0 && c_1_0) begin
            res[1:0] = 2'd2;
        end
    end else res[1:0] = 2'd1;

endfunction


endmodule


