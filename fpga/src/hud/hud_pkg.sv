package hud_pkg;

    typedef struct packed {
        logic active;            // Should sprite be rendered
        logic [7:0] id;          // ID, used for lookup when issued from the MCU
        logic [31:0] mem_base;   // Base pointer for memory location.
        logic [15:0] x;          // X-coordiante for where to paint sprite on screen
        logic [15:0] y;          // Y-coordiante for where to paint sprite on screen
        logic [15:0] w;          // The width of the sprite image
        logic [15:0] h;          // The height of the sprite image
    } sprite_entry_t;

    localparam int SpriteLutDepth = 64; // Number of sprites we can keep track of in the system.

    function automatic void addSprite (
        input sprite_entry_t   lut [SpriteLutDepth],
        input logic [31:0]     mem_base,
        input logic [7:0]      id,
        input logic            active,
        input logic [15:0]     x,
        input logic [15:0]     y,
        input logic [15:0]     w,
        input logic [15:0]     h
    );
        spriteLUT[id].active   = active;
        spriteLUT[id].id       = id;
        spriteLUT[id].mem_base = mem_base;
        spriteLUT[id].x        = x;
        spriteLUT[id].y        = y;
        spriteLUT[id].w        = w;
        spriteLUT[id].h        = h;
    endfunction

    function automatic void updateSprite (
        input sprite_entry_t   lut [SpriteLutDepth],
        input logic [7:0]      id,
        input logic            active,
        input logic [15:0]     x,
        input logic [15:0]     y,
        input logic [15:0]     w,
        input logic [15:0]     h
    );
        spriteLUT[id].active = active;
        spriteLUT[id].x      = x;
        spriteLUT[id].y      = y;
        // spriteLUT[id].w      = w; Dont use, memory isn't updated according to w/h changes
        // spriteLUT[id].h      = h;
    endfunction

    function automatic void deleteSprite (
        input sprite_entry_t   lut [SpriteLutDepth],
        input logic [7:0] id
    );
        spriteLUT[id].active   = 0;
        spriteLUT[id].id       = 0;
        spriteLUT[id].mem_base = 0;
        spriteLUT[id].x        = 0;
        spriteLUT[id].y        = 0;
        spriteLUT[id].w        = 0;
        spriteLUT[id].h        = 0;
    endfunction


endpackage