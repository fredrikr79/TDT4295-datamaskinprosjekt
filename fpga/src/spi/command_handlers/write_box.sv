module write_box #(

) (
    input wire clk,
    ck_rst,
    data_rdy,
    mem_busy,
    input logic [7:0] in_fifo,
    output logic [7:0] mem_in,
    output logic write_cons_done,
    write_cons_r_en,
    mem_toggle,
    output logic [31:0] mem_addr,
    output memory_pkg::transaction_e mem_func
);


  import spi_pck::*;
  import config_pkg::*;
  import mem_pkg::*;

  logic [7:0] row_index, col_index = 8'd0;
  logic [15:0] gridX, gridY;
  logic [15:0] rows, cols;
  logic [31:0] memAddr;
  logic toggle_init;  // Init captures toggle at the start of the transaction.


  read_states_e state = START;

  always_ff @(posedge clk) begin

    if (!ck_rst) begin
      col_index <= 8'd0;
      row_index <= 8'd0;
      write_box_done <= 1'b0;
      write_box_r_en <= 1'b0;
      state <= START;
    end else begin
      write_box_r_en <= 1'b0;
      write_box_done <= 1'b0;

      // Fetch target memory address
      case (state)
        START: begin
          read_cons_r_en <= 1'b1;
          toggle_init <= mem_toggle;
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
          state <= GET_WIDTH_TOP;
        end
        GET_WIDTH_TOP: begin
          write_cons_r_en <= 1'b1;
          cols[15:8] <= in_fifo;
          state <= GET_WIDTH_BOTTOM;
        end
        GET_WIDTH_BOTTOM: begin
          write_cons_r_en <= 1'b1;
          cols[7:0] <= in_fifo;
          state <= GET_HEIGHT_TOP;
        end
        GET_HEIGHT_TOP: begin
          write_cons_r_en <= 1'b1;
          rows[15:8] <= in_fifo;
          state <= GET_HEIGHT_BOTTOM;
        end
        GET_HEIGHT_BOTTOM: begin
          write_cons_r_en <= 1'b1;
          rows[7:0] <= in_fifo;
          state <= GET_MEM_ADDR;
        end
        GET_MEM_ADDR: begin
          memAddr <= GridPosToMemAddr(gridX, gridY + row_index);
          mem_func <= WRITE;
          state <= DATA;
        end
        DATA: begin
          if (!mem_busy) begin
            // use captured toggle to signal the new transaction on the same
            // cycle as data is pushed
            mem_toggle <= !toggle_init;

            // Read N bytes from in_queue to mem_in
            if (col_index <= cols && data_rdy) begin // include final boundary
              mem_in <= in_fifo;
              write_cons_r_en <= 1'b1;
              col_index <= col_index + 8'd1;
            end else begin
              col_index <= 8'd0;

              if(row_index < rows) begin
                row_index <= row_index + 8'd1;
                state <= GET_MEM_ADDR;
              end else begin
                row_index <= 8'd0;
                write_cons_done <= 1'b1;
                state <= START;
              end
            end
          end
        end
        default:;
      endcase
    end
  end
endmodule
