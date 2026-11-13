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

  logic [2:0] settle = 3'b000;
  logic       finish_void;
  import spi_pck::*;


  //State machine
  always @(posedge clk) begin

    if (reset_s) begin
      state <= ST_BOOT;
      error_valid <= 1'b0;
    end else begin

      error_valid <= 0;
      if (error_event.valid && state != ST_FAILED
          && !(state == ST_BUSY && opcode == FAILED)
      ) begin
        state <= ST_FAILED;            // error wins over everything below
      end else begin

        case (state)

          ST_BOOT: begin
            if (boot_done) begin
              state <= ST_READY;
            end
          end

          ST_READY: begin
            if (~ck_ss_s) begin
              settle <= '0;
              state <= ST_RX;
            end
          end

          ST_RX: begin
            if (!ck_ss_s) settle <= '0;
            else if(settle != 3'd7) settle <= settle + 1'd1;
            else if(!in_empty) state <= ST_DECODE;
            else state <= ST_READY;
          end

          ST_DECODE: begin
            state <= ST_BUSY;
          end

          ST_BUSY: begin
            case (opcode)
              UNSET: state <= ST_READY;

              ECHO, INFO, FAILED, READ_CONS, READ_BOX, WRITE_SPRITE, SPRITE_WRITE: begin
                if (proc_done) begin
                  finish_void <= 1'b0;
                  settle      <= '0;
                  state       <= ST_FINISH;
                end
              end

              WRITE_CONS, WRITE_BOX, SPRITE_UPDATE, SPRITE_DELETE, START_SIM, STOP_SIM: begin
                if (proc_done) begin
                  finish_void <= 1'b1;
                  settle      <= '0;
                  state       <= ST_FINISH;
                end
              end

              default: ;
            endcase
          end

          ST_FINISH: begin
            // Let in_empty catch up with the handler's last pop before judging it
            if (settle != 2'd3) begin
              settle <= settle + 1'b1;
            end else if (!in_empty) begin
              error_valid <= 1'b1;
              error_code  <= TOO_MANY_PARAMETERS_ERROR;
            end else begin
              state <= finish_void ? ST_READY : ST_TX_IDLE;
            end
          end

          ST_TX_IDLE: begin  // Wait for TX stage payload from MCU
            if (~ck_ss_s) begin
              state <= ST_TX_SEND;
            end
          end

          ST_TX_SEND: begin
            if (ck_ss_s) begin
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
            error_valid <= 1'b1;
            error_code <= ILLEGAL_STATE_ERROR;
            state <= ST_BUSY;
          end
        endcase
      end
    end
  end

endmodule
