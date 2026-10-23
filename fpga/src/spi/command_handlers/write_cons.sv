module write_cons #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_write_cons,
    mem_busy,
    input logic [7:0] in_fifo,
    output logic [7:0] mem_len,
    mem_in,
    output logic write_cons_done,
    write_cons_r_en,
    mem_req,
    output logic [31:0] mem_addr
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [15:0] total_bytes = 15'b0;
  logic [7:0] written_bytes = 8'd0;
  logic [15:0] tx_index = 8'd0;
  logic [15:0] gridX, gridY;

  mem_states_e state = START;

  always_ff @(posedge clk) begin

    if (ck_rst) begin
      tx_index <= 8'd0;
      write_cons_done <= 1'b0;
      write_cons_r_en <= 1'b0;
      state <= START;
    end else begin
      write_cons_r_en <= 1'b0;
      write_cons_done <= 1'b0;
      mem_req      <= 1'b0;

      if(send_write_cons) begin
        // Fetch target memory address
        case (state)
          START: begin
            write_cons_r_en <= 1'b1;
            state <= GET_X_TOP;
          end
          GET_X_TOP: begin
            write_cons_r_en <= 1'b1;
            gridX[15:8] <= in_fifo;
            state <= GET_X_BOTTOM;
          end
          GET_X_BOTTOM: begin
            write_cons_r_en <= 1'b1;
            gridX[7:0] <= in_fifo;
            state <= GET_Y_TOP;
          end
          GET_Y_TOP: begin
            write_cons_r_en <= 1'b1;
            gridY[15:8] <= in_fifo;
            state <= GET_Y_BOTTOM;
          end
          GET_Y_BOTTOM: begin
            write_cons_r_en <= 1'b1;
            gridY[7:0] <= in_fifo;
            state <= GET_MSG_LENGTH;
          end
          GET_MSG_LENGTH: begin
            total_bytes <= in_fifo;
            mem_len <= in_fifo > memory_pkg::MaxMessageLength
              ? memory_pkg::MaxMessageLength
              : in_fifo;
            state   <= GET_MEM_ADDR;

            if(in_empty) begin
              error_valid <= 1'b1;
              error_code <= MISSING_PARAMETER_ERROR;
            end
          end
          GET_MEM_ADDR: begin
            mem_addr <= GridPosToMemAddr(gridX + tx_index, gridY);
            state <= INIT_REQ;
          end
          INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              state <= DATA;
            end
          end
          DATA: begin
            if (write_cons_r_en) begin
              // Capture data once write_cons is visible to fifo.
              mem_in <= in_fifo;
              written_bytes <= written_bytes + 8'd1;
              tx_index <= tx_index + 8'd1;

              if (tx_index >= total_bytes) begin
                // Command end
                write_cons_r_en <= 1'b0;
                write_cons_done <= 1'b1;
                tx_index <= 8'd0;
                written_bytes <= 8'd0;
                state <= DONE;
              end else if (written_bytes >= memory_pkg::MaxMessageLength - 1) begin
                // Transaction timeout, issue new where we finished.
                write_cons_r_en <= 1'b0;
                state <= GET_MEM_ADDR;
              end else begin
                write_cons_r_en <= mem_data_on_wire;
              end
            end else if (!mem_busy && mem_data_on_wire) begin
              // write byte next cycle.
              write_cons_r_en <= 1'b1;
            end
          end
          DONE: begin
            // Simply wait one cycle for done signal to be processed.
            state <= START;
          end
          default:;
        endcase
      end
    end
  end
endmodule
