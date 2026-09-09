`timescale 1ns / 1ps

module display (
    input wire clk, reset,
    input wire up, down, left, right,
    output wire h_sync, v_sync,
    output wire [2:0] rgb
);
    // Changeable clock speed for display:

    /* 
    Makes display act on a fraction of the 100MHz clock. 100/4 is 25MHz. Must be a power of 2
    Warning: If you change this, simulator.cpp inside vga-simulation needs as many tick()-s in its main loop as the value PIXEL_CLK_DIV has.
    */
    localparam PIXEL_CLK_DIV = 4;

    // Generate pixel_clk signal
    localparam PIXEL_CLK_DIV_W = $clog2(PIXEL_CLK_DIV); // clog2 = ceiling log 2. Figures how many bits needed to represent CLK_DIV.
    initial div_count = 0;
    reg [PIXEL_CLK_DIV_W-1:0] div_count = 0;
    always @(posedge clk)
    begin
        div_count <= div_count + 1'b1;
    end
    wire pixel_clk = div_count[PIXEL_CLK_DIV_W-1]; // Output the top bit of div_count to pixel_clk to signalize a clock pulse on the self-defined clock pixel_clock

    // signal declaration
    wire [9:0] coord_x, coord_y;
    wire active_area;
    
    // instantiate vga_controller circuit 
    vga_controller vga_controller_unit(
        .pixel_clk(pixel_clk), .reset(reset), .h_sync(h_sync), .v_sync(v_sync),
        .coord_x(coord_x), .coord_y(coord_y), .active_area(active_area)
    );
    
    // instantiate graphics generator
    graphics graphics_unit(
        .clk(clk), .reset(reset),
        .up(up), .down(down), .left(left), .right(right),
        .coord_x(coord_x), .coord_y(coord_y),
        .active_area(active_area), .rgb(rgb)
    );

endmodule
