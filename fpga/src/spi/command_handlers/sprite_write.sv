module sprite_write #(

)(
    input wire clk, send_sprite_write,
    input logic [7:0] in_fifo, sprite_write_id,
    output reg [0:0] sprite_write_r_en,
    output hud_pkg::sprite_inst_entry_t spriteLUT [hud_pkg::SpriteLutDepth],
    output logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap
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
    GET_WIDTH_TOP,
    GET_WIDTH_BOTTOM,
    GET_HEIGTH_TOP,
    GET_HEIGTH_BOTTOM,
    EXECUTE
} sprite_write_states_e;

sprite_update_state_e state = START;

logic param_active;
logic [7:0]  param_id, param_mem_id;
logic [15:0] param_x, param_y, param_w, param_h;
logic [31:0] param_mem_addr;

// Allways on req find an available id to hand out to a new sprite write
logic found;
logic [$clog2(SpriteLutDepth)-1:0] available_id;
always_comb begin
    found = 1'b0;
    available_id = '0;

    for (int i = 0; i < SpriteLutDepth; i++) begin
        if(!sprite_id_bitmap[i] && !found) begin
            available_id = i;
            found = 1'b1;
        end
    end
end


always_ff @(posedge clk) begin
    sprite_write_r_en <= 1'b0;

    if(send_sprite_write) begin
        case (state)
            START: begin
                if(found) begin // wait til there is an available slot
                    sprite_write_r_en <= 1'b1;
                    state <= GET_MEM_ID;
                    param_id <= available_id;
                    sprite_id_bitmap[available_id] <= 1'b1;
                end
            end
            GET_MEM_ID: begin
                sprite_write_r_en <= 1'b1;
                param_mem_id <= in_fifo;
                state <= GET_ACTIVE;
            end
            GET_ACTIVE: begin
                sprite_write_r_en <= 1'b1;
                param_active <= in_fifo[0];
                state <= GET_X_TOP;
            end
            GET_X_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_x[15:8] <= in_fifo;
                state <= GET_X_BOTTOM;
            end
            GET_X_BOTTOM: begin
                sprite_write_r_en <= 1'b1;
                param_x[7:0] <= in_fifo;
                state <= GET_Y_TOP;
            end
            GET_Y_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_y[15:8] <= in_fifo;
                state <= GET_Y_BOTTOM;
            end
            GET_Y_BOTTOM: begin
                param_y[7:0] <= in_fifo;
                state <= GET_WIDTH_TOP;
            end
            GET_WIDTH_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_w[15:8] <= in_fifo;
                state <= GET_WIDTH_BOTTOM;
            end
            GET_WIDTH_BOTTOM: begin
                sprite_write_r_en <= 1'b1;
                param_w[7:0] <= in_fifo;
                state <= GET_HEIGTH_TOP;
            end
            GET_HEIGTH_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_h[15:8] <= in_fifo;
                state <= GET_HEIGTH_BOTTOM;
            end
            GET_HEIGTH_BOTTOM: begin
                param_h[7:0] <= in_fifo;
                state <= EXECUTE;
            end
            EXECUTE: begin
                hud_pkg::addSprite(
                    spriteLUT,
                    param_id,
                    param_mem_id,
                    param_active,
                    param_x,
                    param_y,
                    param_h,
                    param_w
                );
                state <= START;
            end
            default:;
        endcase
    end
end


endmodule
