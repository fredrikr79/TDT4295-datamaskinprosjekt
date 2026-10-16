module hud #(

)(
    input hud_pkg::sprite_entry_t [hud_pkg::SpriteLutDepth-1:0] spriteLUT
);

import hud_pkg::*;

// LUT for sprite data each sprite_entry_t includes:
// logic sprite_active;            // Should sprite be rendered
// logic [7:0] sprite_id;          // ID, used for lookup when issued from the MCU
// logic [31:0] sprite_mem_base;   // Base pointer for memory location.
// logic [15:0] sprite_x;          // X-coordiante for where to paint sprite on screen
// logic [15:0] sprite_y;          // Y-coordiante for where to paint sprite on screen
// logic [15:0] sprite_w;          // The width of the sprite image
// logic [15:0] sprite_h;          // The height of the sprite image

endmodule
