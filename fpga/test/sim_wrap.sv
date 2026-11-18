// Thin wrapper around `simulator` with plain vector ports, so the same testbench can drive
// both the RTL and a post-synthesis netlist (array/struct ports get flattened by synthesis).
module sim_wrap (
    input  wire clk,
    input  wire pulse_new_row,
    input  wire sim_running,
    input  wire [3*sim_pkg::CellGridWidth*sim_pkg::BitsPerPixel-1:0] in_flat,
    output wire pulse_transaction_pair_done,
    output wire pulse_row_done,
    output wire [2*sim_pkg::CellGridWidth*sim_pkg::BitsPerPixel-1:0] out_flat
);
  localparam int W = sim_pkg::CellGridWidth * sim_pkg::BitsPerPixel;

  sim_pkg::row_buffer_t in_rows[3];
  sim_pkg::row_buffer_t out_rows[2];

  for (genvar i = 0; i < 3; i++) begin : g_in
    assign in_rows[i] = in_flat[i*W+:W];
  end
  for (genvar i = 0; i < 2; i++) begin : g_out
    assign out_flat[i*W+:W] = out_rows[i];
  end

  simulator u_sim (
      .clk(clk),
      .pulse_new_row(pulse_new_row),
      .sim_running(sim_running),
      .input_buffers(in_rows),
      .pulse_transaction_pair_done(pulse_transaction_pair_done),
      .pulse_row_done(pulse_row_done),
      .output_buffers(out_rows)
  );
endmodule