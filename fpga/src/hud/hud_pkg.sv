package hud_pkg;

    typedef struct packed {
        logic        active;   // Should sprite be rendered
        logic [7:0]  id;       // ID, used for lookup when issued from the MCU
        logic [31:0] mem_id;   // Used for lookup in internal LUT for sprite location on memory.
        logic [15:0] x;        // X-coordiante for where to paint sprite on screen
        logic [15:0] y;        // Y-coordiante for where to paint sprite on screen
        logic [15:0] w;        // The width of the sprite image
        logic [15:0] h;        // The height of the sprite image
    } sprite_entry_t;

    localparam int SpriteLutDepth = 64; // Number of sprites we can keep track of in the system.

    function automatic void addSprite (
        ref   sprite_entry_t   lut [SpriteLutDepth],
        input logic [7:0]      id,
        input logic [7:0]      mem_id,
        input logic            active,
        input logic [15:0]     x,
        input logic [15:0]     y,
        input logic [15:0]     w,
        input logic [15:0]     h
    );
        lut[id].active   = active;
        lut[id].id       = id;
        lut[id].mem_id   = mem_id;
        lut[id].x        = x;
        lut[id].y        = y;
        lut[id].w        = w;
        lut[id].h        = h;
    endfunction

    function automatic void updateSprite (
        ref   sprite_entry_t   lut [SpriteLutDepth],
        input logic [7:0]      id,
        input logic            active,
        input logic [15:0]     x,
        input logic [15:0]     y
    );
        lut[id].active = active;
        lut[id].x      = x;
        lut[id].y      = y;
    endfunction

    function automatic void deleteSprite (
        ref   sprite_entry_t   lut [SpriteLutDepth],
        input logic [7:0] id
    );
        lut[id].active   = 0;
        lut[id].mem_id       = 0;
        lut[id].mem_base = 0;
        lut[id].x        = 0;
        lut[id].y        = 0;
        lut[id].w        = 0;
        lut[id].h        = 0;
    endfunction


endpackage