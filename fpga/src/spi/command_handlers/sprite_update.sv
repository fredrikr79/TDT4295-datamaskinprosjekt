module sprite_update #(

)(
    input wire clk, send_sprite_update,
    input reg [0:0] sprite_delete_r_en,
    input logic [7:0] in_fifo,
    output hud_pkg::sprite_entry_t spriteLUT [hud_pkg::SpriteLutDepth]
);

import hud_pkg::*;


typedef enum {
    START,
    GET_ID,
    GET_ACTIVE,
    GET_X_TOP,
    GET_X_BOTTOM,
    GET_Y_TOP,
    GET_Y_BOTTOM,
    EXECUTE
} sprite_delete_states_e;

sprite_update_state_e state = START;

logic param_active;
logic [7:0] param_id;
logic [15:0] param_x, param_y;

always_ff @(posedge clk) begin
    sprite_delete_r_en <= 1'b0;

    if(send_sprite_update) begin
        case (state)
            START: begin
                sprite_delete_r_en <= 1'b1;
                state <= GET_ID;
            end
            GET_ID: begin
                sprite_delete_r_en <= 1'b1;
                param_id <= in_fifo;
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
                hud_pkg::updateSprite(spriteLUT, param_id, param_active, param_x, param_y);
                state <= START;
            end
            default:;
        endcase
    end
end


endmodule
