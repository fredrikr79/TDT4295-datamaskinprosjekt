module write_box #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_write_box,
    mem_busy,
    input logic [7:0] in_fifo,
    output logic [7:0] mem_in,
    mem_len,
    output logic write_box_done,
    write_box_r_en,
    mem_req,
    output logic [31:0] mem_addr
);

  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  typedef enum bit [4:0] {
    START,
    GET_X_TOP,
    GET_X_BOTTOM,
    GET_Y_TOP,
    GET_Y_BOTTOM,
    GET_WIDTH_TOP,
    GET_WIDTH_BOTTOM,
    GET_HEIGHT_TOP,
    GET_HEIGHT_BOTTOM,
    GET_MEM_ADDR,
    DATA,
    DONE
  } w_box_states_e;

  logic [15:0] row_index, col_index = 8'd0;
  logic [15:0] gridX, gridY;
  logic [15:0] rows, cols;

  w_box_states_e state = START;

  always_ff @(posedge clk) begin

    if (ck_rst) begin
      col_index <= 8'd0;
      row_index <= 8'd0;
      write_box_done <= 1'b0;
      write_box_r_en <= 1'b0;
      mem_req        <= 1'b0;
      state <= START;
    end else begin
      write_box_r_en <= 1'b0;
      write_box_done <= 1'b0;
      mem_req        <= 1'b0;

      if(send_write_box) begin
        case (state)
          START: begin
            write_box_r_en <= 1'b1;
            state <= GET_X_TOP;
          end
          GET_X_TOP: begin
            write_box_r_en <= 1'b1;
            gridX[15:8] <= in_fifo;
            state <= GET_X_BOTTOM;
          end
          GET_X_BOTTOM: begin
            write_box_r_en <= 1'b1;
            gridX[7:0] <= in_fifo;
            state <= GET_Y_TOP;
          end
          GET_Y_TOP: begin
            write_box_r_en <= 1'b1;
            gridY[15:8] <= in_fifo;
            state <= GET_Y_BOTTOM;
          end
          GET_Y_BOTTOM: begin
            write_box_r_en <= 1'b1;
            gridY[7:0] <= in_fifo;
            state <= GET_WIDTH_TOP;
          end
          GET_WIDTH_TOP: begin
            write_box_r_en <= 1'b1;
            cols[15:8] <= in_fifo;
            state <= GET_WIDTH_BOTTOM;
          end
          GET_WIDTH_BOTTOM: begin
            write_box_r_en <= 1'b1;
            cols[7:0] <= in_fifo;
            state <= GET_HEIGHT_TOP;
          end
          GET_HEIGHT_TOP: begin
            write_box_r_en <= 1'b1;
            rows[15:8] <= in_fifo;
            state <= GET_HEIGHT_BOTTOM;
          end
          GET_HEIGHT_BOTTOM: begin
            rows[7:0] <= in_fifo;
            state <= GET_MEM_ADDR;

            if(in_empty) begin
              error_valid <= 1'b1;
              error_code <= MISSING_PARAMETER_ERROR;
            end
          end
          GET_MEM_ADDR: begin
            mem_req <= 1'b1;
            mem_len <= cols;
            mem_addr <= GridPosToMemAddr(gridX, gridY + row_index);
            state <= DATA;
          end
          DATA: begin
            if (!mem_busy) begin
              // use captured toggle to signal the new transaction on the same
              // cycle as data is pushed

              if (mem_data_on_wire) begin
                write_box_r_en <= 1'b1;
              end
              if(write_box_r_en) begin
                mem_in <= in_fifo;

                // Read N bytes from in_queue to mem_in
                if (col_index >= cols - 1) begin // include final boundary
                  col_index <= 16'd0;
                  write_box_r_en <= 1'b0;

                  if(row_index < rows - 1) begin
                    row_index <= row_index + 16'd1;
                    state <= GET_MEM_ADDR;
                  end else begin
                    row_index <= 16'd0;
                    write_box_done <= 1'b1;
                    state <= DONE;
                  end
                end else begin
                  col_index <= col_index + 16'd1;
                  write_box_r_en <= mem_data_on_wire;
                end
              end
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
