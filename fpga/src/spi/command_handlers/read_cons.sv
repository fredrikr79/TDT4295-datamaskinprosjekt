module read_cons #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_read_cons,
    mem_busy,
    input logic [7:0] mem_out,
    in_fifo,
    output logic [7:0] read_cons_fifo,
    output logic [15:0]    mem_len,
    output logic read_cons_done,
    read_cons_w_en,
    read_cons_r_en,
    mem_req,
    output logic [31:0] mem_addr
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [15:0] rx_index = 16'd0;
  logic [15:0] gridX, gridY;

  mem_states_e state = START;

  always_ff @(posedge clk) begin

    if (ck_rst) begin
      rx_index <= 16'd0;
      read_cons_done <= 1'b0;
      read_cons_w_en <= 1'b0;
      read_cons_r_en <= 1'b0;
      read_cons_fifo <= 8'd0;
      state <= START;

    end else begin
      read_cons_w_en <= 1'b0;
      read_cons_r_en <= 1'b0;
      read_cons_done <= 1'b0;
      mem_req        <= 1'b0;

      if (send_read_cons) begin
        // Fetch target memory address
        case (state)
          START: begin
            read_cons_r_en <= 1'b1;
            state <= GET_X_TOP;
          end
          GET_X_TOP: begin
            read_cons_r_en <= 1'b1;
            gridX[15:8] <= in_fifo;
            state <= GET_X_BOTTOM;
          end
          GET_X_BOTTOM: begin
            read_cons_r_en <= 1'b1;
            gridX[7:0] <= in_fifo;
            state <= GET_Y_TOP;
          end
          GET_Y_TOP: begin
            read_cons_r_en <= 1'b1;
            gridY[15:8] <= in_fifo;
            state <= GET_Y_BOTTOM;
          end
          GET_Y_BOTTOM: begin
            read_cons_r_en <= 1'b1;
            gridY[7:0] <= in_fifo;
            state <= GET_MSG_LENGTH;
          end
          GET_MSG_LENGTH: begin
            mem_len <= 16'(in_fifo);
            state   <= GET_MEM_ADDR;
          end
          GET_MEM_ADDR: begin
            mem_addr <= GridPosToMemAddr(gridX, gridY);
            state <= INIT_REQ;
          end
          INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              state   <= DATA;
            end
          end
          DATA: begin
            if (!mem_busy) begin
              if (rx_index >= mem_len) begin
                read_cons_done <= 1'b1;
                rx_index <= 16'd0;
                state <= DONE;
              end else if (mem_data_on_wire) begin
                read_cons_w_en <= 1'b1;
                read_cons_fifo <= mem_out;
                rx_index <= rx_index + 16'd1;
              end
            end
          end
          DONE: begin
            // Simply wait one cycle for done signal to be processed.
            state <= START;
          end
          default: ;
        endcase
      end
    end
  end
endmodule
