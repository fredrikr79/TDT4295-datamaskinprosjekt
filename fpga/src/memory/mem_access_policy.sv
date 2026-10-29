module mem_access_policy (
    input logic v_blank,
    input logic near_frame_end,
    input logic sim_running,

    output logic [memory_pkg::NumMemClients-1:0] allow
);

  always_comb begin
    allow = '0;
    allow[memory_pkg::MemClientScanoutID] = 1'b1;
    allow[memory_pkg::MemClientHudID] = 1'b1;
    allow[memory_pkg::MemClientSpiID] = !sim_running || (v_blank && !near_frame_end);
    allow[memory_pkg::MemClientCellsID] = 1'b1;
  end

endmodule
