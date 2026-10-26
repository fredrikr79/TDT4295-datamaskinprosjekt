module hud #(

)(
    input wire clk, req_valid, write_sprite_valid,
    input hud_pkg::sprite_mem_id sprite_mem_id,
    input [31:0] sprite_w_mem_addr,
    input hud_pkg::sprite_op_e sprite_req_op,
    input hud_pkg::sprite_inst_entry_t sprite_req_body,
    output hud_pkg::sprite_inst_entry_t spriteLUT [hud_pkg::SpriteLutDepth],
    output logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
    output hud_pkg::sprite_lut_id sprite_available_id,
    output logic sprite_id_found
);

import hud_pkg::*;

// Allways on req find an available id to hand out to a new sprite write
logic found;
always_comb begin
    found = 1'b0;
    sprite_available_id = '0;

    for (int i = 0; i < SpriteLutDepth; i++) begin
        if(!sprite_id_bitmap[i] && !found) begin
            sprite_available_id = hud_pkg::sprite_lut_id'(i);
            found = 1'b1;
        end
    end
end

// Allocate LUT for SpriteMemDepth number of sprite id <-> base pointer pairs
logic [31:0] sprite_mem_lut [SpriteMemDepth];

always_ff @(posedge clk) begin
    if(write_sprite_valid) begin
        sprite_mem_lut[sprite_mem_id] <= sprite_w_mem_addr;
    end
end

always_ff @(posedge clk) begin
    if(req_valid) begin
        case(sprite_req_op)
            LUT_WRITE: begin
                addSprite(spriteLUT, sprite_id_bitmap, sprite_req_body);
            end
            LUT_UPDATE: begin
                updateSprite(spriteLUT, sprite_req_body);
            end
            LUT_DELETE: begin
                deleteSprite(spriteLUT, sprite_id_bitmap, sprite_req_body.id);
            end
            default:;
        endcase
    end
end


endmodule

