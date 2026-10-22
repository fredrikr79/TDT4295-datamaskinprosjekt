module spi_fsm #(

) (
    input spi_pck::opcode_e opcode,
    input logic reset_s,
    boot_done,
    ck_ss_s,
    clk,
    in_empty,
    out_full,
    proc_done,
    input spi_pck::error_event_t error_event,  // Incoming global error decision
    output logic error_valid,  // outgoing error proposition
    output logic [7:0] error_code,
    output spi_pck::state_e state
);

  import spi_pck::*;

  //State machine
  always @(posedge clk) begin

    if (reset_s) begin
      state <= ST_BOOT;

      if (error_event.valid) begin
        // Error code owned by SPI_processor
        state <= ST_FAILED;
      end

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
          case (opcode)
            ECHO: begin
              if (in_empty && proc_done) begin
                state <= ST_TX_IDLE;
              end
            end
            INFO: begin
              if (in_empty && proc_done) begin
                state <= ST_TX_IDLE;
              end
            end
            FAILED: begin
              if (in_empty && proc_done) state <= ST_TX_IDLE;
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
          error_event_m.valid <= 1'b1;
          error_event_m.code <= ILLEGAL_STATE_ERROR;
          state <= ST_BUSY;
        end
      endcase

    end
  end

endmodule
