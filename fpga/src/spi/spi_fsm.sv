module spi_fsm #(

) (
    input spi_pck::opcode_e opcode,
    input logic reset_s, boot_done, ck_ss_s, clk, in_empty, out_full,
    output spi_pck::state_e state
);

  import spi_pck::*;

  //State machine
  always @(posedge clk) begin

    if (reset_s) begin
      state <= ST_BOOT;

    end else begin
      case (state)

        ST_BOOT: begin
          if (boot_done) begin
            state <= ST_READY;
          end
        end

        ST_READY: begin
          if (~ck_ss_s) begin
            state <= ST_RX;
          end
        end

        ST_RX: begin
          if (ck_ss_s && !in_empty) begin
            state <= ST_DECODE;
          end
        end

        ST_DECODE: begin
          state <= ST_BUSY;
        end

        ST_BUSY: begin
          //TODO refactor into spi_proc, interface must be defined.
          case (opcode)
            ECHO: begin
              if (in_empty) begin
                state <= ST_TX_IDLE;
              end
            end
            FAILED: begin
                state <= ST_TX_IDLE;
            end
            default: begin
              state <= ST_FAILED;
            end
          endcase
        end

        ST_TX_IDLE: begin  // Wait for TX stage payload from MCU
          if (~ck_ss_s && ~in_empty) begin
            state <= ST_TX_SEND;
          end
        end

        ST_TX_SEND: begin
          if (ck_ss_s && in_empty) begin
            state <= ST_READY;  // Reset device
          end
        end

        ST_MEMORY: begin
          //TODO implement
          state <= ST_READY;
        end

        ST_FAILED: begin
          if (!out_full) begin
            // FAILED process description sets the opcode
            // to a designated failed procedure in spi_processor
            state <= ST_BUSY;
          end
        end

        default: begin
          // Go to fail state handler
          state <= ST_BUSY;
        end
      endcase

    end
  end

endmodule
