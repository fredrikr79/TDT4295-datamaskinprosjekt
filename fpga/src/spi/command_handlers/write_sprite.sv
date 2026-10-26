module write_sprite #(

) (
    input wire clk,
    ck_rst,
    mem_data_on_wire,
    send_write_sprite,
    mem_busy,
    input logic in_empty,
    input logic [7:0] in_fifo,
    output logic [15:0] mem_len,
    output logic write_sprite_done,
    write_sprite_r_en,
    write_sprite_w_en,
    mem_req,
    write_sprite_valid,
    error_valid,
    output logic [7:0] write_sprite_fifo,
    mem_in,
    error_code,
    output logic [31:0] mem_addr = 32'd0,
    output hud_pkg::sprite_mem_id write_sprite_mem_id
);

  import hud_pkg::*;
  import spi_pck::*;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  logic [15:0] total_bytes = 16'd0;
  logic [7:0] written_bytes = 8'd0;
  logic [15:0] tx_index = 16'd0;
  logic [15:0] gridX, gridY;

  mem_driver_pkg::mem_states_e state = MEM_START;
  hud_pkg::sprite_mem_id next_id = 0;

  always_ff @(posedge clk) begin

    if (ck_rst) begin
      tx_index <= 16'd0;
      write_sprite_fifo <= 8'd0;
      state <= MEM_START;
    end else begin
      write_sprite_r_en  <= 1'b0;
      write_sprite_w_en  <= 1'b0;
      write_sprite_done  <= 1'b0;
      write_sprite_valid <= 1'b0;
      mem_req            <= 1'b0;
      error_valid        <= 1'b0;

      if (send_write_sprite) begin
        // Fetch target memory address
        case (state)
          MEM_START: begin
            if (32'(next_id) >= hud_pkg::SpriteMemDepth) begin
              error_valid <= 1'b1;
              error_code <= RESOURCE_EXHAUSTED_ERROR;
            end else begin
              write_sprite_r_en <= 1'b1;
              state <= MEM_GET_MSG_LENGTH_TOP;
            end
          end
          MEM_GET_MSG_LENGTH_TOP: begin
            //TODO fix MSG is two bytes
            write_sprite_r_en <= 1'b1;
            total_bytes[15:8] <= in_fifo;
            state <= MEM_GET_MSG_LENGTH_BOTTOM;
          end
          MEM_GET_MSG_LENGTH_BOTTOM: begin
            total_bytes[7:0] <= in_fifo;
            state <= MEM_INIT_REQ;
            if(in_empty) begin
              error_valid <= 1'b1;
              error_code <= MISSING_PARAMETER_ERROR;
            end
          end
          MEM_INIT_REQ: begin
            // The signal is delayed one cycle in the SPI controller.
            if (!mem_busy) begin
              mem_req <= 1'b1;
              mem_len <= 32'(total_bytes) > memory_pkg::MaxMessageLength
                ? 16'(memory_pkg::MaxMessageLength)
                : total_bytes;
              state   <= MEM_DATA;
            end
          end
          MEM_DATA: begin
            // Read N bytes from in_queue to mem_in
            if(write_sprite_r_en) begin
              mem_in <= in_fifo;
              written_bytes <= written_bytes + 8'd1;
              tx_index <= tx_index + 16'd1;

              if (tx_index >= total_bytes) begin
                // Clean up transaction state
                write_sprite_r_en <= 1'b0;
                mem_addr <= mem_addr + 32'(mem_len);
                written_bytes <= 8'd0;
                tx_index <= 16'd0;
                write_sprite_valid <= 1'b1;
                write_sprite_mem_id  <= next_id;
                state <= MEM_RETURN_TOP;

              end else if (written_bytes >= 8'(memory_pkg::MaxMessageLength - 1)) begin
                mem_addr <= mem_addr + memory_pkg::MaxMessageLength;
                write_sprite_r_en <= 1'b0;
                state <= MEM_INIT_REQ;
              end
            end else if (!mem_busy && mem_data_on_wire) begin
              write_sprite_r_en <= 1'b1;
            end
          end
          MEM_RETURN_TOP: begin
            if($bits(hud_pkg::sprite_lut_id) > 8) begin
                write_sprite_w_en <= 1'b1;
                write_sprite_fifo <= 8'(next_id[$bits(hud_pkg::sprite_lut_id)-1:8]);
            end
            state <= MEM_RETURN_BOTTOM;
            end
          MEM_RETURN_BOTTOM: begin
            write_sprite_w_en <= 1'b1;
            write_sprite_fifo <= next_id[7:0];
            write_sprite_done <= 1'b1;
            next_id <= next_id + hud_pkg::sprite_mem_id'(1);
            state <= MEM_DONE;
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
