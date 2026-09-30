module info #(

) (
    input logic clk,
    out_full,
    output logic info_done, info_w_en,
    output logic [7:0] out_fifo
);

  import config_pkg::*;

  logic [95:0] data;
  logic [3:0] cycle_counter = 4'b0000;
  logic [31:0] sessionID = 32'h0F0F0F0F;  // SEED

  logic nextbit;
  assign nextbit = sessionID[8]
                ^ sessionID[12]
                ^ sessionID[14]
                ^ sessionID[15]
                ^ sessionID[17]
                ^ sessionID[21];

  always_ff @(posedge clk) begin
    sessionID <= {sessionID[30:0], nextbit};
  end

  // This task should be run until "done" flag is raised
  assign data[95:64] = {config_pkg::ActiveWidth, config_pkg::ActiveHeight};
  assign data[63:32] = {16'd1024, 16'd1024};
  assign data[31:0]  = sessionID;

  task static sendInfo;

    if(info_w_en === 1'b0) begin
        info_w_en <= 1'b1;
    end
    else if(cycle_counter === 4'b1011) begin
        info_done <= 1'b1;
        info_w_en <= 0;
    end
    else if(!out_full) begin
        info_done <= 1'b0;
        info_w_en <= 1'b1;
        out_fifo <= data[95 - cycle_counter*8 -: 8];
        cycle_counter <= cycle_counter + 1;
    end
    else begin
        info_w_en <= 1'b0;
    end
  endtask

endmodule
