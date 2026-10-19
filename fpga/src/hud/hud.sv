module hud #(

)(
    input wire clk,
    input hud_pkg::sprite_inst_entry_t [hud_pkg::SpriteLutDepth-1:0] spriteLUT,
    input [7:0] sprite_mem_id,
    input [31:0] sprite_w_mem_addr
);

import hud_pkg::*;

// LUT for sprite data each sprite_inst_entry_t includes:
// logic sprite_active;            // Should sprite be rendered
// logic [7:0] sprite_id;          // ID, used for lookup when issued from the MCU
// logic [31:0] sprite_mem_base;   // Base pointer for memory location.
// logic [15:0] sprite_x;          // X-coordiante for where to paint sprite on screen
// logic [15:0] sprite_y;          // Y-coordiante for where to paint sprite on screen
// logic [15:0] sprite_w;          // The width of the sprite image
// logic [15:0] sprite_h;          // The height of the sprite image

// Allocate LUT for SpriteMemDepth number of sprite id <-> base pointer pairs
logic [31:0] sprite_mem_lut [SpriteMemDepth];

always_ff @(posedge clk) begin
    sprite_mem_lut[sprite_mem_id] <= sprite_w_mem_addr;
end

endmodule

