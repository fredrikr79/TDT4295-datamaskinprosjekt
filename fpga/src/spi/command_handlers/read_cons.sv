module read_cons #(
    logic [7:0] N_BYTES = config_pkg::activeWidth
) (
    input wire clk,
    ck_rst, data_rdy,
    input logic [7:0] mem_out_fifo, in_fifo,
    output logic [7:0] read_cons_fifo, mem_len, mem_in_fifo,
    output logic read_cons_done,
    read_cons_w_en,
    read_cons_r_en
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_pkg::*;

  logic [7:0] rx_index = 8'd0;
  logic [15:0] gridX, gridY;
  logic [7:0] length;
  logic [31:0] memAddr;
  logic toggle_init; // Init captures toggle at the start of the transaction.

  read_states_e state = START;

  always_ff @(posedge clk) begin

    if (!ck_rst) begin
          rx_index <= 8'd0;
          read_cons_done <= 1'b0;
          read_cons_w_en <= 1'b0;
          read_cons_r_en <= 1'b0;
          read_cons_fifo <= 8'd0;
          state <= START;

    end else begin
      read_cons_w_en <= 1'b0;
      read_cons_r_en <= 1'b0;
      read_cons_done <= 1'b0;

      // Fetch target memory address
      case(state)
        START: begin
          read_cons_r_en <= 1'b1;
          state <= GET_X_TOP;
          toggle_init <= toggle;
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
          length <= in_fifo;
          state <= GET_MEM_ADDR;
        end
        GET_MEM_ADDR: begin
          memAddr <= GridPosToMemAddr(gridX, gridY);
          state <= DATA;
        end
        DATA: begin
          if(!mem_busy) begin
            mem_len <= length;
            mem_func <= WRITE;
            mem_addr <= memAddr;
            state <= RX_WAIT;
          end
        end
        RX_WAIT: begin
          if(data_rdy) begin
            state <= RX;
          end
        end
        RX: begin
          toggle <= !toggle_init;

          if(data_rdy) begin
            read_cons_fifo <= mem_out_fifo;
            read_cons_w_en <= 1'b1;
          end else begin
            rx_index <= 8'd0;
            read_cons_done <= 1'b1;
            state <= START;
          end
        end
        default: begin
          // Do nothing
        end
      endcase
    end
  end
endmodule