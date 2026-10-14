module cells (
    input  wire                             clk,
    input  wire [3:2]                       sw,

    input logic                             active_area, 
                                            pixel_pulse,

    input vga_sync_params::x_coordinate_t   coord_x,
    input vga_sync_params::y_coordinate_t   coord_y,
    output color_pkg::color_t               color
);
    wire we;
    wire [cell_pkg::AddrBits-1:0] write_addr, read_addr;
    cell_pkg::cell_word_t write_data, read_data;

    world_loader loader (
        .clk  (clk),
        .reset(sw[3]),
        .we   (we),
        .write_addr (write_addr),
        .data (write_data)
    );

    cell_memory memory (
        .clk       (clk),
        .we        (we),
        .write_addr(write_addr),
        .write_data(write_data),
        .read_addr (read_addr),
        .read_data (read_data)
    );

    cell_renderer renderer (
        .active_area(active_area),
        .coord_x    (coord_x),
        .coord_y    (coord_y),
        .read_addr  (read_addr),
        .read_data  (read_data),
        .color      (color)
    );
endmodule
