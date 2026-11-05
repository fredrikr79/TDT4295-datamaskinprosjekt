module read_cons #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_read_cons,
    mem_busy,
    in_empty,
    input logic [7:0] mem_out,
    in_fifo,
    output logic [7:0] read_cons_fifo,
    error_code,
    output logic [15:0] mem_len,
    output logic read_cons_done,
    read_cons_w_en,
    read_cons_r_en,
    mem_req,
    error_valid,
    output logic [31:0] mem_addr
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [7:0] read_bytes = 8'd0;
  logic [15:0] total_bytes = 16'b0;
  logic [15:0] rx_index = 16'd0;
  logic [15:0] gridX, gridY;

  mem_driver_pkg::mem_states_e state = MEM_START;

  always_ff @(posedge clk) begin

    if (ck_rst) begin
      rx_index <= 16'd0;
      read_cons_done <= 1'b0;
      read_cons_w_en <= 1'b0;
      read_cons_r_en <= 1'b0;
      read_cons_fifo <= 8'd0;
      state <= MEM_START;

    end else begin
      read_cons_w_en <= 1'b0;
      read_cons_r_en <= 1'b0;
      read_cons_done <= 1'b0;
      mem_req        <= 1'b0;

      if (send_read_cons) begin
        // Fetch target memory address
        case (state)
          MEM_START: begin
            read_cons_r_en <= 1'b1;
            state <= MEM_GET_X_TOP;
          end
          MEM_GET_X_TOP: begin
            read_cons_r_en <= 1'b1;
            gridX[15:8] <= in_fifo;
            state <= MEM_GET_X_BOTTOM;
          end
          MEM_GET_X_BOTTOM: begin
            read_cons_r_en <= 1'b1;
            gridX[7:0] <= in_fifo;
            state <= MEM_GET_Y_TOP;
          end
          MEM_GET_Y_TOP: begin
            read_cons_r_en <= 1'b1;
            gridY[15:8] <= in_fifo;
            state <= MEM_GET_Y_BOTTOM;
          end
          MEM_GET_Y_BOTTOM: begin
            read_cons_r_en <= 1'b1;
            gridY[7:0] <= in_fifo;
            state <= MEM_GET_MSG_LENGTH_TOP;
          end
          MEM_GET_MSG_LENGTH_TOP: begin
              read_cons_r_en <= 1'b1;
              total_bytes[15:8] <= in_fifo;
              state <= MEM_GET_MSG_LENGTH_BOTTOM;
            end
          MEM_GET_MSG_LENGTH_BOTTOM: begin
              read_cons_r_en <= 1'b1;
              total_bytes[7:0] <= in_fifo;
              state <= MEM_GET_MEM_ADDR;
              if(in_empty) begin
                error_valid <= 1'b1;
                error_code <= MISSING_PARAMETER_ERROR;
              end
          end
          MEM_GET_MEM_ADDR: begin
            mem_addr <= GridPosToMemAddr(gridX + rx_index, gridY);
            mem_len <= 32'(total_bytes - rx_index) > memory_pkg::MaxMessageLength
              ? 16'(memory_pkg::MaxMessageLength)
              : (total_bytes - rx_index);
            state <= MEM_INIT_REQ;
          end
          MEM_INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              state <= MEM_DATA;
            end
          end
          MEM_DATA: begin
            if (read_cons_w_en) begin
              // Capture data once write_cons is visible to fifo.
              read_cons_fifo <= mem_out;
              read_bytes <= read_bytes + 8'd1;
              rx_index <= rx_index + 16'd1;

              if (rx_index >= total_bytes - 1) begin
                // Command end
                read_cons_w_en <= 1'b0;
                read_cons_done <= 1'b1;
                rx_index <= 16'd0;
                read_bytes <= 8'd0;
                state <= MEM_DONE;
              end else if (read_bytes >= 8'(memory_pkg::MaxMessageLength - 1)) begin
                // Transaction timeout, issue new where we finished.
                read_cons_w_en <= 1'b0;
                read_bytes <= 8'd0;
                state <= MEM_GET_MEM_ADDR;
              end else begin
                read_cons_w_en <= mem_data_on_wire;
              end
            end else if (!mem_busy && mem_data_on_wire) begin
              // write byte next cycle.
              read_cons_w_en <= 1'b1;
            end
          end
          MEM_DONE: begin
            // Simply wait one cycle for done signal to be processed.
            state <= MEM_START;
          end
          default: ;
        endcase
      end
    end
  end
endmodule
