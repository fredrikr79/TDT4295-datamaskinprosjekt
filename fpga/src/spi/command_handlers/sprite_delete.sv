module sprite_delete #(

)(
    input wire clk, send_sprite_delete,
    input reg sprite_delete_r_en,
    input logic [7:0] in_fifo,
    output logic sprite_delete_done,
    output hud_pkg::sprite_inst_entry_t spriteLUT [hud_pkg::SpriteLutDepth],
    output logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap
);

import hud_pkg::*;


typedef enum bit [1:0] {
    START,
    GET_ID,
    EXECUTE,
    DONE
} sprite_delete_states_e;

sprite_delete_state_e state = START;

logic [7:0] param_id;

always_ff @(posedge clk) begin
    sprite_delete_r_en <= 1'b0;
    sprite_delete_done <= 1'b0;

    if(send_sprite_delete) begin
        case (state)
            START: begin
                sprite_delete_r_en <= 1'b1;
                state <= GET_ID;
            end
            GET_ID: begin
                param_id <= in_fifo;
                state <= EXECUTE;
            end
            EXECUTE: begin
                hud_pkg::deleteSprite(spriteLUT, param_id);
                sprite_id_bitmap[param_id] <= 1'b0;
                sprite_delete_done <= 1'b1;
                state <= DONE;
            end
            DONE: begin
                // Simply wait one cycle for done signal to be processed.
                state <= START;
            end
            default: ;
        endcase
    end
end


endmodule
