// Interface-only stubs for the Vivado FIFO IP, used by make lint only.
// These do not model FIFO behavior and must not be used for simulation.
/* verilator lint_off DECLFILENAME */
/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNDRIVEN */
module in_buffer (
    input wire [7:0] din,
    output wire [7:0] dout,
    output wire almost_full, full, almost_empty, empty,
    input wire wr_en, rd_en,
    output wire wr_ack,
    input wire rst, wr_clk, rd_clk,
    output wire wr_rst_busy, rd_rst_busy
);
endmodule

module out_buffer (
    input wire [7:0] din,
    output wire [7:0] dout,
    output wire almost_full, full, almost_empty, empty,
    input wire wr_en, rd_en, rst, wr_clk, rd_clk,
    output wire wr_rst_busy, rd_rst_busy
);
endmodule
/* verilator lint_on UNDRIVEN */
/* verilator lint_on UNUSEDSIGNAL */
/* verilator lint_on DECLFILENAME */
