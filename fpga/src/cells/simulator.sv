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

// Find pixel neighbourhood in parallell:
always @(posedge clk) begin

    if(pulse_new_row && sim_running) begin
        row_running <= 1'b1;
    end

    for(i = sim_pkg::SimulationCoreCount-1; i >= 0; i = i + 1) begin : process_lane

        neighbourhood_t neighbourhood;
        automatic logic curr  = (base + i) * sim_pkg::BitsPerPixel;
        automatic logic left  = (base + i - 1) * sim_pkg::BitsPerPixel;
        automatic logic right = (base + i + 1) * sim_pkg::BitsPerPixel;

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

        // Add simulation handlers below
        // Proposed mechanism: Either do sequential automata evaluation
        // Or parallellize cell candidate and use conflict resolution to
        // Deal with conflicts.
        res_buffer[curr +: sim_pkg::BitsPerPixel] <= sim_gol(neighbourhood);



        // Update base index
        base <= base + sim_pkg::SimulationCoreCount;
    end
end

function automatic material_t sim_gol(sim_pkg::neighbourhood_t stencil);
    automatic logic [3:0] neighbour_count = 4'd0;
    automatic integer r, c;

    neighbour_count =
        props(stencil.top[7:4]).solid == 1'b1 +
        props(stencil.center[7:4]).solid == 1'b1 +
        props(stencil.bottom[7:4]).solid == 1'b1 +
        props(stencil.topLeft[7:4]).solid == 1'b1 +
        props(stencil.left[7:4]).solid == 1'b1 +
        props(stencil.bottomLeft[7:4]).solid == 1'b1 +
        props(stencil.topRight[7:4]).solid == 1'b1 +
        props(stencil.right[7:4]).solid == 1'b1 +
        props(stencil.bottomRight[7:4]).solid == 1'b1;

    return (neighbour_count | (stencil.center[7:4] == sim_pkg::MatGOL)) == 4'd3
        ? {MatGOL , 4'b0}
        : {MatAir , 4'b0};
endfunction

endmodule


