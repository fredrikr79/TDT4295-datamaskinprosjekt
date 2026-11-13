package hud_pkg;

    localparam int SpriteMemDepth = 1024; // Number of sprites we can keep on memory.
    typedef bit [15:0] sprite_mem_id;

    localparam int SpriteLutDepth = 1024; // Number of sprites we can keep track of in the system.
    typedef bit [15:0] sprite_lut_id;
    localparam int unsigned LutBits = $clog2(hud_pkg::SpriteLutDepth);  // 10

    typedef struct packed {
        logic        active;   // Should sprite be rendered
        sprite_lut_id  id;       // ID, used for lookup when issued from the MCU
        sprite_mem_id  mem_id;   // Used for lookup in internal LUT for sprite location on memory.
        logic [15:0] x;        // X-coordiante for where to paint sprite on screen
        logic [15:0] y;        // Y-coordiante for where to paint sprite on screen
        logic [15:0] w;        // The width of the sprite image
        logic [15:0] h;        // The height of the sprite image
    } sprite_inst_entry_t;

    typedef struct packed {
        sprite_mem_id mem_id;
        logic [15:0] w;
        logic [15:0] h;
    } static_sprite_data_t;

    typedef struct packed {
        logic active;
        logic [15:0] x;
        logic [15:0] y;
    } dynamic_sprite_data_t;

    typedef enum bit [1:0] {
        NONE,
        LUT_WRITE,
        LUT_UPDATE,
        LUT_DELETE
    } sprite_op_e;

endpackage
