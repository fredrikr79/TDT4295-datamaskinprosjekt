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
    mem_toggle,
    output logic [31:0] mem_addr,
    output memory_pkg::transaction_e mem_transaction
);

  import hud_pkg::*;
  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [7:0] tx_index = 8'd0;
  logic [15:0] gridX, gridY;

  read_states_e state = START;

  initial mem_addr = 32'd0; // Keep track of sprite base pointer
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
      mem_toggle        <= 1'b0;

      if(send_write_sprite) begin
        // Fetch target memory address
        case (state)
          START: begin
            write_sprite_r_en <= 1'b1;
            state <= GET_X_TOP;
          end
          GET_MSG_LENGTH: begin
            mem_len <= in_fifo;
            state   <= GET_MEM_ADDR;
          end
          GET_MEM_ADDR: begin
            mem_transaction <= WRITE;
            state <= DATA;
          end
          DATA: begin
            if (!mem_busy) begin
              // use captured toggle to signal the new transaction on the same
              // cycle as data is pushed
              mem_toggle <= 1'b1;

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
