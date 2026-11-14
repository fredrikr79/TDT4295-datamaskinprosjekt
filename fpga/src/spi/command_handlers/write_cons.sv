module write_cons #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_write_cons,
    mem_busy,
    input logic in_empty,
    input logic [7:0] in_fifo,
    output logic [7:0] mem_in,
    error_code,
    output logic [15:0] mem_len,
    output logic write_cons_done,
    write_cons_r_en,
    mem_req,
    error_valid,
    output logic [31:0] mem_addr
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_map_pkg::*;
  import memory_pkg::*;

  logic [15:0] total_bytes = 16'b0;
  logic [15:0] written_bytes = 16'd0;
  logic [15:0] tx_index = 16'd0;
  logic [15:0] gridX, gridY;

  mem_map_pkg::mem_states_e state = MEM_START;
  logic do_r_en;
  logic data_pop;

  assign data_pop        = send_write_cons && (state == MEM_DATA) && mem_data_on_wire;
  assign write_cons_r_en = do_r_en | data_pop;
  assign mem_in          = in_fifo;      // FWFT head = byte for the current slot


  always_ff @(posedge clk) begin

    if (ck_rst) begin
      tx_index <= 16'd0;
      write_cons_done <= 1'b0;
      do_r_en <= 1'b0;
      state <= MEM_START;
    end else begin
      error_valid <= 1'b0;
      do_r_en <= 1'b0;
      write_cons_done <= 1'b0;
      mem_req         <= 1'b0;

      if (send_write_cons) begin
        // Fetch target memory address
        case (state)
          MEM_START: begin
            do_r_en <= 1'b1;
            state <= MEM_GET_X_TOP;
          end
          MEM_GET_X_TOP: begin
            do_r_en <= 1'b1;
            gridX[15:8] <= in_fifo;
            state <= MEM_GET_X_BOTTOM;
          end
          MEM_GET_X_BOTTOM: begin
            do_r_en <= 1'b1;
            gridX[7:0] <= in_fifo;
            state <= MEM_GET_Y_TOP;
          end
          MEM_GET_Y_TOP: begin
            do_r_en <= 1'b1;
            gridY[15:8] <= in_fifo;
            state <= MEM_GET_Y_BOTTOM;
          end
          MEM_GET_Y_BOTTOM: begin
            do_r_en <= 1'b1;
            gridY[7:0] <= in_fifo;
            state <= MEM_GET_MSG_LENGTH_TOP;
          end
          MEM_GET_MSG_LENGTH_TOP: begin
            //TODO fix MSG is two bytes
            do_r_en <= 1'b1;
            total_bytes[15:8] <= in_fifo;
            state <= MEM_GET_MSG_LENGTH_BOTTOM;
          end
          MEM_GET_MSG_LENGTH_BOTTOM: begin
            total_bytes[7:0] <= in_fifo;
            state <= MEM_GET_MEM_ADDR;
            if(in_empty) begin
              error_valid <= 1'b1;
              error_code <= MISSING_PARAMETER_ERROR;
            end
          end
          MEM_GET_MEM_ADDR: begin
            mem_addr <= GridPosToMemAddr(gridX + tx_index, gridY);
            mem_len <= 32'(total_bytes - tx_index) > memory_pkg::MaxMessageLength
              ? 16'(memory_pkg::MaxMessageLength)
              : (total_bytes - tx_index);
            state <= MEM_INIT_REQ;
          end
          MEM_INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              state   <= MEM_DATA;
            end
          end
        MEM_DATA: begin
          if (mem_data_on_wire) begin
            written_bytes <= written_bytes + 16'd1;
            tx_index      <= tx_index + 16'd1;
            if (tx_index >= total_bytes - 1) begin
              write_cons_done <= 1'b1;
              tx_index        <= 16'd0;
              written_bytes   <= 16'd0;
              state           <= MEM_DONE;
            end else if (written_bytes >= 16'(memory_pkg::MaxMessageLength - 1)) begin
              written_bytes <= 16'd0;
              state         <= MEM_GET_MEM_ADDR;
            end
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
