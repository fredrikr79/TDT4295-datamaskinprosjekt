module write_sprite #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_write_sprite,
    mem_busy,
    input logic [7:0] in_fifo,
    output logic [7:0] write_sprite_fifo,
    mem_len,
    mem_in,
    output logic write_sprite_done,
    write_sprite_r_en,
    write_sprite_w_en,
    mem_req,
    write_sprite_valid,
    output logic [31:0] mem_addr
);

  import hud_pkg::*;
  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [7:0] tx_index = 8'd0;
  logic [15:0] gridX, gridY;

  read_states_e state = START;

  logic [$clog2(SpriteMemDepth)-1:0] next_id = 0;

  always_ff @(posedge clk) begin

    if (!ck_rst) begin
      tx_index <= 8'd0;
      write_sprite_fifo <= 8'd0;
      state <= START;
    end else begin
      write_sprite_r_en <= 1'b0;
      write_sprite_w_en <= 1'b0;
      write_sprite_done <= 1'b0;
      mem_req           <= 1'b0;

      if(send_write_sprite) begin
        // Fetch target memory address
        case (state)
          START: begin
            write_sprite_r_en <= 1'b1;
            state <= GET_MSG_LENGTH;
          end
          GET_MSG_LENGTH: begin
            mem_len <= in_fifo;
            state   <= GET_MEM_ADDR;
          end
          GET_MEM_ADDR: begin
            state <= DATA;
          end
          INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              state <= DATA;
            end
          end
          DATA: begin
            if (!mem_busy) begin
                // Read N bytes from in_queue to mem_in
                if (tx_index >= mem_len) begin
                  write_sprite_done <= 1'b1;
                  write_sprite_w_en <= 1'b1;
                  tx_index <= 8'd0;
                  mem_addr <= mem_addr + mem_len;
                  write_sprite_fifo <= next_id;
                  next_id <= next_id + 1'b1;
                  state <= DONE;
                end else if (mem_data_on_wire) begin
                  mem_in <= in_fifo;
                  write_sprite_r_en <= 1'b1;
                  tx_index <= tx_index + 8'd1;
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
