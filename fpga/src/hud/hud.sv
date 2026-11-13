module hud #(

)(
    input wire clk, req_valid, write_sprite_valid,
    input hud_pkg::sprite_mem_id sprite_mem_id,
    input [31:0] sprite_w_mem_addr,
    input hud_pkg::sprite_op_e sprite_req_op,
    input hud_pkg::sprite_inst_entry_t sprite_req_body,

    // Sprite instance memory entry lookup for BRAM implementation
    // Write signal resides in req body.
    input hud_pkg::sprite_lut_id sprite_lut_read_id,

    output hud_pkg::sprite_inst_entry_t sprite_read_entry,
    output logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap = '0,
    output logic [15:0] sprite_available_id,
    output logic sprite_id_found,
    input hud_pkg::sprite_mem_id lookup_mem_id,
    output logic [31:0] lookup_mem_addr
);

import hud_pkg::*;

// Allways on req find an available id to hand out to a new sprite write

localparam int PtrW = (hud_pkg::SpriteLutDepth > 1) ? $clog2(hud_pkg::SpriteLutDepth) : 1;
logic [PtrW-1:0] sprite_ptr = '0;
always_ff @(posedge clk) begin
    if(!sprite_id_bitmap[sprite_ptr]) begin
        sprite_available_id <= hud_pkg::sprite_lut_id'(sprite_ptr);
        sprite_id_found <= 1'b1;
    end else begin
        sprite_id_found <= 1'b0;
        if (sprite_ptr == PtrW'(hud_pkg::SpriteLutDepth - 1)) sprite_ptr <= '0;
        else sprite_ptr <= sprite_ptr + 1'b1;
    end
end

// Allocate LUT for SpriteMemDepth number of sprite id <-> base pointer pairs
logic [31:0] sprite_mem_lut [SpriteMemDepth];

always_ff @(posedge clk) begin
    if(write_sprite_valid) begin
        sprite_mem_lut[sprite_mem_id] <= sprite_w_mem_addr;
    end
end

assign lookup_mem_addr = sprite_mem_lut[lookup_mem_id];

hud_pkg::sprite_op_e op;
assign op = req_valid ? sprite_req_op : hud_pkg::NONE;

hud_pkg::static_sprite_data_t static_w_data;
hud_pkg::dynamic_sprite_data_t dynamic_w_data;
hud_pkg::static_sprite_data_t static_r_data;
hud_pkg::dynamic_sprite_data_t dynamic_r_data;

(* ram_style = "block" *) hud_pkg::static_sprite_data_t static_data_mem [hud_pkg::SpriteLutDepth];
(* ram_style = "block" *) hud_pkg::dynamic_sprite_data_t dynamic_data_mem [hud_pkg::SpriteLutDepth];


assign dynamic_w_data = op == hud_pkg::LUT_DELETE
    ? '0
    : '{
        active: sprite_req_body.active,
        x: sprite_req_body.x,
        y: sprite_req_body.y
    };

assign static_w_data = '{
        mem_id: sprite_req_body.mem_id,
        w:      sprite_req_body.w,
        h:      sprite_req_body.h
    };

hud_pkg::sprite_lut_id read_req_id;

always_comb begin
    sprite_read_entry = '0;
    sprite_read_entry.active = dynamic_r_data.active;
    sprite_read_entry.id = read_req_id;
    sprite_read_entry.mem_id = static_r_data.mem_id;
    sprite_read_entry.x = dynamic_r_data.x;
    sprite_read_entry.y = dynamic_r_data.y;
    sprite_read_entry.w = static_r_data.w;
    sprite_read_entry.h = static_r_data.h;
end

always_ff @(posedge clk) begin
    // Write data
    read_req_id <= sprite_lut_read_id;
    if(op inside {hud_pkg::LUT_WRITE, hud_pkg::LUT_UPDATE}) begin
        dynamic_data_mem[sprite_req_body.id] <= dynamic_w_data;
        if(op == hud_pkg::LUT_WRITE) begin
            sprite_id_bitmap[sprite_req_body.id] <= 1'b1;
            static_data_mem[sprite_req_body.id] <= static_w_data;
        end
    end else if (op == hud_pkg::LUT_DELETE) begin
        sprite_id_bitmap[sprite_req_body.id] <= 1'b0;
    end

    // Read data
    static_r_data <= static_data_mem[sprite_lut_read_id];
    dynamic_r_data <= dynamic_data_mem[sprite_lut_read_id];

end

endmodule

