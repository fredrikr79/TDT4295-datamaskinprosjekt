module hud #(

)(
    input wire clk, req_valid,
    input [7:0] sprite_mem_id,
    input [31:0] sprite_w_mem_addr,
    input hud_pkg::sprite_op_e req_op,
    input hud_pkg::sprite_inst_entry_t req_body,
    output hud_pkg::sprite_inst_entry_t spriteLUT [hud_pkg::SpriteLutDepth],
    output logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
    output logic [$clog2(hud_pkg::SpriteLutDepth)-1:0] available_id
);

import hud_pkg::*;

// Allways on req find an available id to hand out to a new sprite write
logic found;
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
    if(req_valid) begin
        case(req_op)
            WRITE: begin
                addSprite(spriteLUT, sprite_id_bitmap, req_body);
            end
            UPDATE: begin
                updateSprite(spriteLUT, req_body);
            end
            DELETE: begin
                deleteSprite(spriteLUT, sprite_id_bitmap, req_body);
            end
            default:;
        endcase
    end
end


endmodule

