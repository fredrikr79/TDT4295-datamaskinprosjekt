package mem_driver_pkg;

    parameter int unsigned GridMemLocationStart = 32'd0;
    parameter int unsigned BytesPerGridPixel = 4'd2;
    parameter int unsigned SpriteMemLocationStart = config_pkg::ActiveWidth
        *config_pkg::ActiveHeight
        * BytesPerGridPixel;


    // Memory transaction states
    typedef enum bit [3:0] {
        START,
        GET_X_TOP,
        GET_X_BOTTOM,
        GET_Y_TOP,
        GET_Y_BOTTOM,
        GET_MSG_LENGTH,
        GET_MEM_ADDR,
        DATA,
        DONE
    } mem_state_e;

    function static logic [31:0] GridPosToMemAddr (input logic [15:0] x, input logic [15:0] y);
        return GridMemLocationStart + y * config_pkg::ActiveWidth + x;
    endfunction
endpackage