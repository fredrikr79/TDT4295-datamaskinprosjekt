module spi_processor #(

) (
    input spi_pck::state_e state,
    input logic [7:0] in_fifo,
    input logic out_full,
    in_empty,
    reset_s,
    clk,
    ck_ss_s,
    output logic [7:0] out_fifo,
    output logic proc_w_en,
    proc_r_en,
    output spi_pck::opcode_e opcode
);

  import spi_pck::*;

  // in_read handlers registration MUX proc_1 | proc_2 | ...
  logic echo_r_en = 1'b0;
  logic fail_r_en = 1'b0;
  assign proc_r_en = echo_r_en | fail_r_en;

  // out_write handlers registration MUX proc_1 | proc_2 | ...
  logic echo_w_en = 1'b0;
  logic fail_w_en = 1'b0;
  assign proc_w_en = echo_w_en | fail_w_en;

  always_ff @(posedge clk) begin

    // general driver for popping data on IN_FIFO and driving onto OUT_FIFO
    // During TX / TX. Issued under proc to allow the processes to keep ownership over data flow
    // Processes are registered in DECODE, it is recommended to MUX read / write toggles to subprocesses
    // e.g in_r_en = cmd1_in_r_en | cmd2_in_r_en

    if (reset_s) begin
      echo_r_en  <= 1'b0;
      echo_w_en <= 1'b0;
      fail_w_en <= 1'b0;
      fail_r_en <= 1'b0;
      opcode <= spi_pck::opcode_e'(4'h0);
      out_fifo <= 8'h00;
    end else begin

      echo_r_en  <= 1'b0;
      echo_w_en <= 1'b0;
      fail_w_en <= 1'b0;
      fail_r_en <= 1'b0;

      case (state)

        ST_FAILED: begin
          opcode <= FAILED;
        end

        ST_DECODE: begin
           // USING FWFT FIFO's allowing us to peek at the read bus without popping.
            // Each dispatched command decides for itself if it should pop the command
            // By setting in_r_en <= 1'b1;
            opcode <= spi_pck::opcode_e'(in_fifo[7:4]);
        end

        ST_BUSY: begin
            case (spi_pck::opcode_e'(opcode))

              // Register command handler here. they should own their own read/write signals.
              // Register these in the in_r_en / out_w_en MUX assignments.
              // Out_fifo is owned by this always_ff block and is the intended output register
              // for any response data.

              ECHO: begin
                if (!out_full && !in_empty) begin
                    out_fifo <= in_fifo;
                    echo_w_en <= 1'b1;
                    echo_r_en  <= 1'b1;
                end
              end

              FAILED: begin
                if (!out_full) begin
                  if(!in_empty) begin
                    fail_r_en <= 1'b1;
                  end
                  else begin
                    out_fifo <= 8'hE0;
                    fail_w_en <= 1'b1;
                    opcode <= UNSET;
                  end
                end
              end

              default: begin
                opcode <= FAILED;
              end
          endcase
        end

        default: begin
          // Do nothing
        end

      endcase
    end
  end

endmodule
