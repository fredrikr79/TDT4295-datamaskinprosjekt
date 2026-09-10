module main(
    input wire clk,
    input wire [3:0] sw,
    input  wire [3:0] btn,
    output reg  [3:0] led,
    output wire [3:0] vga_r, vga_g, vga_b,
    output wire       vga_hsync, vga_vsync
);

    reg [26:0] counter;

    always @(posedge clk) begin
       counter <=  counter + sw;
    
        if (counter >= 26'd49_999_999) begin
             counter <= 0;
             led <= led + 1'b1;
        end
    end

    wire h_sync, v_sync;
    wire [2:0] rgb;

    display display_unit(
        .clk(clk),
        .reset(sw[3]),
        .up(btn[0]), .down(btn[1]), .left(btn[2]), .right(btn[3]),
        .h_sync(h_sync), .v_sync(v_sync), .rgb(rgb)
    );

    // rgb[0]=R, rgb[1]=G, rgb[2]=B (confirmed by the colour constants in
    // graphics.v: 3'b010 is green, 3'b101 is magenta).
    // Replicate each bit across the Pmod's 4-bit channel: 0 or full scale.
    assign vga_r = {4{rgb[0]}};
    assign vga_g = {4{rgb[1]}};
    assign vga_b = {4{rgb[2]}};

    // 640x480@60 needs NEGATIVE sync polarity; vga_controller drives its
    // sync pulses active-high, so invert here.
    assign vga_hsync = ~h_sync;
    assign vga_vsync = ~v_sync;
endmodule