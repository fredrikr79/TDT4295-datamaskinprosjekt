module simulator #(

)(
    input wire clk, pulse_new_row, sim_running,
    input sim_pkg::row_buffer_t buffers [3],
    output logic pulse_row_done,
    output sim_pkg::row_buffer_t res_buffer
);

import sim_pkg::*;

integer base = 0;
integer i;
logic row_running = 1'b0;
logic [15:0] stride = sim_pkg::CellGridWidth / sim_pkg::SimulationCoreCount;

// Find pixel neighbourhood in parallell:
always @(posedge clk) begin

    if(pulse_new_row && sim_running) begin
        row_running <= 1'b1;
    end

    for(i = sim_pkg::SimulationCoreCount-1; i >= 0; i = i + 1) begin : process_lane

        sim_pkg::neighbourhood_t neighbourhood;
        sim_pkg::material_t explo_mat, gol_mat, sand_mat, fluid_mat;


        // i * stride = base addr for sim core. base = round offset.
        automatic logic curr  = (base + i * stride) * sim_pkg::BitsPerPixel;
        automatic logic left  = (base + i * stride - 1) * sim_pkg::BitsPerPixel;
        automatic logic right = (base + i * stride + 1) * sim_pkg::BitsPerPixel;


        if(i + base >= config_pkg::ActiveWidth || !row_running) begin
            base <= 0;
            row_running <= 1'b0;
            pulse_row_done <= 1'b1;
            continue;
        end

        neighbourhood = '{default: MatAir};

            neighbourhood.top         = buffers[0][curr +: sim_pkg::BitsPerPixel];
            neighbourhood.center      = buffers[1][curr +: sim_pkg::BitsPerPixel];
            neighbourhood.bottom      = buffers[2][curr +: sim_pkg::BitsPerPixel];

        if(i + base >= config_pkg::ActiveWidth || !row_running) begin
            if(row_running)
                pulse_row_done <= 1'b1;
            base <= 0;
            row_running <= 1'b0;
            continue;
        end

        if((base + i) > 0) begin
            neighbourhood.topLeft     = buffers[0][left +: sim_pkg::BitsPerPixel];
            neighbourhood.left        = buffers[1][left +: sim_pkg::BitsPerPixel];
            neighbourhood.bottomLeft  = buffers[2][left +: sim_pkg::BitsPerPixel];
        end

        if(right != null) begin
            neighbourhood.topRight    = buffers[0][right +: sim_pkg::BitsPerPixel];
            neighbourhood.right       = buffers[1][right +: sim_pkg::BitsPerPixel];
            neighbourhood.bottomRight = buffers[2][right +: sim_pkg::BitsPerPixel];
        end

        // Priority based situation resolution. Evaluation is done in parallell
        // To avoid race conditions each frame will only translate pixels in
        // Either the left or right direction (in addition to vertical)

        // Functions return 1'b1 if match was found in the rule

        explo_mat <= explosion_rules(neighbourhood);
        gol_mat <= gol_rules(neighbourhood);
        sand_mat <= sand_rules(neighbourhood);
        fluid_mat <= fluid_rules(neighbourhood);

        if (explo_mat != 8'hFF) begin
            res_buffer[curr +: sim_pkg::BitsPerPixel] <= explo_mat;
        end else if (gol_mat != 8'hFF) begin
            res_buffer[curr +: sim_pkg::BitsPerPixel] <= gol_mat;
        end else if (sand_mat != 8'hFF) begin
            res_buffer[curr +: sim_pkg::BitsPerPixel] <= sand_mat;
        end else if (fluid_mat != 8'hFF) begin
            res_buffer[curr +: sim_pkg::BitsPerPixel] <= fluid_mat;
        end else begin
            res_buffer[curr +: sim_pkg::BitsPerPixel] <= neighbourhood.center;
        end

        // Update base index
        base <= base + sim_pkg::SimulationCoreCount;
    end
end

function automatic logic [7:0] explosion_rules(sim_pkg::neighbourhood_t stencil);
    logic [3:0] p1 = (stencil.topLeft[7:4]    == sim_pkg::MatExplo ? stencil.topLeft[3:0]    :4'd0)-
                     (stencil.bottomRight[7:4]== sim_pkg::MatExplo ? stencil.bottomRight[3:0]:4'd0);
    logic [3:0] p2 = (stencil.top[7:4]        == sim_pkg::MatExplo ? stencil.top[3:0]        :4'd0)-
                     (stencil.bottom[7:4]     == sim_pkg::MatExplo ? stencil.bottom[3:0]     :4'd0);
    logic [3:0] p3 = (stencil.topRight[7:4]   == sim_pkg::MatExplo ? stencil.topRight[3:0]   :4'd0)-
                     (stencil.bottomLeft[7:4] == sim_pkg::MatExplo ? stencil.bottomleft[3:0] :4'd0);
    logic [3:0] p4 = (stencil.right[7:4]      == sim_pkg::MatExplo ? stencil.right[3:0]      :4'd0)-
                     (stencil.left[7:4]       == sim_pkg::MatExplo ? stencil.left[3:0]      :4'd0);
    logic [3:0] blast_resistance = props(stencil.center[7:4]).blast_resistance;

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

    force_strength = p1 + p2 + p3 + p4 - blast_resistance;

    return force_strength > 0
        ? {sim_pkg::MatExplo, force_strength}
        : 8'hFF;
endfunction

function automatic logic [7:0] gol_rules(sim_pkg::neighbourhood_t stencil);
    automatic logic [3:0] neighbour_count =
        (stencil.top[7:4]         == sim_pkg::MatGOL) +
        (stencil.center[7:4]      == sim_pkg::MatGOL) +
        (stencil.bottom[7:4]      == sim_pkg::MatGOL) +
        (stencil.topLeft[7:4]     == sim_pkg::MatGOL) +
        (stencil.left[7:4]        == sim_pkg::MatGOL) +
        (stencil.bottomLeft[7:4]  == sim_pkg::MatGOL) +
        (stencil.topRight[7:4]    == sim_pkg::MatGOL) +
        (stencil.right[7:4]       == sim_pkg::MatGOL) +
        (stencil.bottomRight[7:4] == sim_pkg::MatGOL);

    if(props(stencil.center).solid) begin
        return (neighbour_count | (stencil.center[7:4] == sim_pkg::MatGOL)) == 4'd3
            ? {MatGOL , 4'b0}
            : 8'hFF;
    end
endfunction

function automatic logic [7:0] sand_rules(sim_pkg::neighbourhood_t stencil, logic r);
    logic [3:0] c_n1_n1 = stencil.topLeft[7:4]     == sim_pkg::MatSand
                      && (stencil.topLeft[3:0]     == 0);
    logic [3:0] c_n1_0 = stencil.left[7:4]         == sim_pkg::MatSand
                      && (stencil.left[3:0]        == 0);
    logic [3:0] c_n1_1 = stencil.bottomLeft[7:4]   == sim_pkg::MatSand
                      && (stencil.bottomLeft[3:0]  == 0);
    logic [3:0] c_0_n1 = stencil.top[7:4]          == sim_pkg::MatSand
                      && (stencil.top[3:0]         == 0);
    logic [3:0] c_0_0  = stencil.center[7:4]       == sim_pkg::MatSand
                      && (stencil.center[3:0]      == 0);
    logic [3:0] c_0_1  = stencil.bottom[7:4]       == sim_pkg::MatSand
                      && (stencil.bottom[3:0]      == 0);
    logic [3:0] c_1_n1  = stencil.topRight[7:4]    == sim_pkg::MatSand
                      && (stencil.topRight[3:0]    == 0);
    logic [3:0] c_1_0  = stencil.right[7:4]        == sim_pkg::MatSand
                      && (stencil.right[3:0]       == 0);
    logic [3:0] c_1_1  = stencil.bottomRight[7:4]  == sim_pkg::MatSand
                      && (stencil.bottomRight[3:0] == 0);

    if (c_0_0 > 0) begin
        stencil.center[3:0] = stencil.center[3:0] - 4'd1;
        if (c_0_1 == 4'b0) begin
            return {sim_pkg::MatAir, 4'b0};
        end else if (r && !c_1_1 && !c_1_0) begin
            return {sim_pkg::MatAir, 4'b0};
        end else if (!r && !c_n1_1 && !c_n1_0) begin
            return {sim_pkg::MatAir, 4'b0};
        end
    end else begin
        if (c_0_n1) begin
            return stencil.top;
            stencil.center[3:0] = sim_pkg::SandCounter;
        end else if (r && c_n1_n1 && !c_n1_0) begin
            return stencil.topLeft;
            stencil.center[3:0] = sim_pkg::SandCounter;
        end else if (!r && c_1_n1 && !c_1_0) begin
            return stencil.topRight;
            stencil.center[3:0] = sim_pkg::SandCounter;
        end
    end
endfunction

function automatic logic [7:0] fluid_rules(sim_pkg::neighbourhood_t stencil);
    return 8'hFF;
endfunction

function automatic logic [7:0] sim_explo(sim_pkg::neighbourhood_t stencil);
    // Simulate explosions
    return 8'hFF;
endfunction

endmodule


