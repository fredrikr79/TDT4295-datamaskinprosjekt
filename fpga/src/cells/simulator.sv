module simulator #(

)(
    input wire clk, pulse_new_row, sim_running,
    input sim_pkg::row_buffer buffers [3],
    output wire pulse_row_done,
    output sim_pkg::row_buffer res_buffer
);

import sim_pkg::*;

integer base = 0;
logic row_running = 1'b0;

// Find pixel neighbourhood in parallell:
always @(posedge clk) begin

    if(pulse_new_row && sim_running) begin
        row_running <= 1'b1;
    end

    integer i;
    for(i = sim_pkg::SimulationCoreCount-1; i >= 0; i = i + 1) begin

        if(i + base >= config_pkg::ActiveWidth || !row_running) begin
            base <= 0;
            row_running <= 1'b0;
            pulse_row_done <= 1'b1;
            continue;
        end

        // Relative indexes
        automatic integer curr = (base + i) * sim_pkg::BitsPerPixel;
        automatic integer left = (base + i) > 0
            ? (base + i - 1) * sim_pkg::BitsPerPixel
            : 32'hFFFFFFFF;
        automatic integer right = (base + i) < config_pkg::ActiveWidth-1
            ? (base + i + 1) * sim_pkg::BitsPerPixel
            : 32'hFFFFFFFF;

        // Calculate neighbourhood3
        automatic logic [15:0] neighbourhood [3][3] = 0;

        neighbourhood[0][1] = buffers[0][curr +: sim_pkg::BitsPerPixel];
        neighbourhood[1][1] = buffers[1][curr +: sim_pkg::BitsPerPixel];
        neighbourhood[2][1] = buffers[2][curr +: sim_pkg::BitsPerPixel];

        if(left != null) begin
            neighbourhood[0][0] = buffers[0][left +: sim_pkg::BitsPerPixel];
            neighbourhood[1][0] = buffers[1][left +: sim_pkg::BitsPerPixel];
            neighbourhood[2][0] = buffers[2][left +: sim_pkg::BitsPerPixel];
        end

        if(right != null) begin
            neighbourhood[0][2] = buffers[0][right +: sim_pkg::BitsPerPixel];
            neighbourhood[1][2] = buffers[1][right +: sim_pkg::BitsPerPixel];
            neighbourhood[2][2] = buffers[2][right +: sim_pkg::BitsPerPixel];
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

function automatic material_t sim_gol(neigbourhood_t stencil);
    automatic logic [3:0] neighbourhood_count = 4'd0;
    for(int r = 0; r < 3; r++) begin
        for(int c = 0; c < 3; c++) begin
            if(r != 1 || c != 1 && stencil[r][c] != sim_pkg::MatAir) begin
                neighbourhood_count++;
            end
        end
    end
    return (neighbourhood_count | (stencil[1][1][7:4] == sim_pkg::MatGOL)) == 4'd3
        ? {MatGOL , 4'b0}
        : {MatAir , 4'b0};
endfunction

endmodule


