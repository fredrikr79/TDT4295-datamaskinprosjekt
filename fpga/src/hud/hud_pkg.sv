package hud_pkg;

    // 
    localparam int SpriteMemDepth = 64; // Number of sprites we can keep on memory.

    typedef struct packed {
        logic        active;   // Should sprite be rendered
        logic [7:0]  id;       // ID, used for lookup when issued from the MCU
        logic [7:0]  mem_id;   // Used for lookup in internal LUT for sprite location on memory.
        logic [15:0] x;        // X-coordiante for where to paint sprite on screen
        logic [15:0] y;        // Y-coordiante for where to paint sprite on screen
        logic [15:0] w;        // The width of the sprite image
        logic [15:0] h;        // The height of the sprite image
    } sprite_inst_entry_t;

    localparam int SpriteLutDepth = 256; // Number of sprites we can keep track of in the system.

    typedef enum bit [1:0] {
        LUT_WRITE,
        LUT_UPDATE,
        LUT_DELETE
    } sprite_op_e;

    function automatic void addSprite (
        ref   sprite_inst_entry_t   lut [SpriteLutDepth],
        logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
        sprite_inst_entry_t body
    );
        sprite_id_bitmap[body.id] = 1'b1;
        lut[body.id]              = body;
    endfunction

    function automatic void updateSprite (
        ref   sprite_inst_entry_t   lut [SpriteLutDepth],
        sprite_inst_entry_t body
    );
        lut[body.id].active = body.active;
        lut[body.id].x      = body.x;
        lut[body.id].y      = body.y;
    endfunction

    function automatic void deleteSprite (
        ref   sprite_inst_entry_t   lut [SpriteLutDepth],
        logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
        input logic [7:0] id
    );
        sprite_id_bitmap[id]  = 1'b0;
        lut[id].active        = 0;
        lut[id].mem_id        = 0;
        lut[id].mem_base      = 0;
        lut[id].x             = 0;
        lut[id].y             = 0;
        lut[id].w             = 0;
        lut[id].h             = 0;
    endfunction


endpackage