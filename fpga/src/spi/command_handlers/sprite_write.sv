module sprite_write #(

)(
    input wire clk, send_sprite_write,
    input reg [0:0] sprite_delete_r_en,
    input logic [7:0] in_fifo, sprite_write_id,
    output hud_pkg::sprite_entry_t spriteLUT [hud_pkg::SpriteLutDepth]
);

import hud_pkg::*;


typedef enum {
    START,
    GET_MEM_ID,
    GET_ACTIVE,
    GET_X_TOP,
    GET_X_BOTTOM,
    GET_Y_TOP,
    GET_Y_BOTTOM,
    EXECUTE
} sprite_delete_states_e;

sprite_update_state_e state = START;

logic param_active;
logic [7:0]  param_id, param_mem_id;
logic [15:0] param_x, param_y, param_w, param_h;
logic [31:0] param_mem_addr;

always_ff @(posedge clk) begin
    sprite_delete_r_en <= 1'b0;

    if(send_sprite_write) begin
        case (state)
            START: begin
                sprite_delete_r_en <= 1'b1;
                state <= GET_MEM_ID;
            end

            GET_MEM_ID: begin
                sprite_delete_r_en <= 1'b1;
                param_mem_id <= in_fifo;
                state <= GET_ACTIVE;
            end
            GET_ACTIVE: begin
                sprite_delete_r_en <= 1'b1;
                param_active <= in_fifo[0];
                state <= GET_X_TOP;
            end
            GET_X_TOP: begin
                sprite_delete_r_en <= 1'b1;
                param_x[15:8] <= in_fifo;
                state <= GET_X_BOTTOM;
            end
            GET_X_BOTTOM: begin
                sprite_delete_r_en <= 1'b1;
                param_x[7:0] <= in_fifo;
                state <= GET_Y_TOP;
            end
            GET_Y_TOP: begin
                sprite_delete_r_en <= 1'b1;
                param_x[15:8] <= in_fifo;
                state <= GET_Y_BOTTOM;
            end
            GET_Y_BOTTOM: begin
                param_x[7:0] <= in_fifo;
                state <= EXECUTE;
            end
            EXECUTE: begin
                hud_pkg::addSprite(
                    spriteLUT,
                    param_active,
                    param_id,
                    param_mem_id,
                    param_x,
                    param_y,
                    param_w,
                    param_h
                );
                state <= START;
            end
            default:;
        endcase
    end
end


endmodule
