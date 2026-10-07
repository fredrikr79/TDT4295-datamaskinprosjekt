module info #(

) (
    input logic clk,
    out_full,
    send_info,
    output logic info_done, info_w_en,
    output logic [7:0] info_fifo
);

  import config_pkg::*;

  logic [31:0] sessionID = 32'h0F0F0F0F;  // SEED
  logic [3:0] cycle_counter = 4'b0000;
  wire [79:0] data = {config_pkg::ActiveWidth,
                    config_pkg::ActiveHeight,
                    16'd1024,
                    sessionID};;

  // First time issuing INFO command?
  reg locked = 1'b0;

  // Randomizer for bitstream
  logic nextbit;
  assign nextbit = sessionID[8]
                ^ sessionID[12]
                ^ sessionID[14]
                ^ sessionID[15]
                ^ sessionID[17]
                ^ sessionID[21];

  always_ff @(posedge clk) begin
    if(!locked && !send_info) begin
        sessionID <= {sessionID[30:0], nextbit};
    end

    if(send_info) begin
        locked <= 1'b1;
    end

    if(!send_info) begin
        cycle_counter <= 4'd0;
        info_done <= 1'b0;
        info_w_en <= 1'b0;
    end else begin
        sendInfo();
    end

  end

  task static sendInfo;

    info_w_en <= 1'b0;

    if(cycle_counter == 4'd10) begin
        info_done <= 1'b1;
    end
    else if(!out_full && !info_w_en) begin
        info_w_en <= 1'b1;
        info_fifo <= data[79 - cycle_counter*8 -: 8];
        cycle_counter <= cycle_counter + 1;
    end
  endtask

endmodule
