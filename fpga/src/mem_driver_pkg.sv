package mem_driver_pkg;

    parameter int unsigned GridMemLocationStart = 32'd0;
    parameter int unsigned BytesPerGridPixel = 1;
    parameter int unsigned SpriteMemLocationStart = config_pkg::ActiveWidth
        *config_pkg::ActiveHeight
        * BytesPerGridPixel;


    // Memory transaction states
    typedef enum bit [3:0] {
        MEM_START,
        MEM_GET_X_TOP,
        MEM_GET_X_BOTTOM,
        MEM_GET_Y_TOP,
        MEM_GET_Y_BOTTOM,
        MEM_GET_MSG_LENGTH_TOP,
        MEM_GET_MSG_LENGTH_BOTTOM,
        MEM_GET_MEM_ADDR,
        MEM_INIT_REQ,
        MEM_DATA,
        MEM_RETURN_TOP,
        MEM_RETURN_BOTTOM,
        MEM_DONE
    } mem_states_e;

    function static logic [31:0] GridPosToMemAddr (input logic [15:0] x, input logic [15:0] y);
        return GridMemLocationStart + y * config_pkg::ActiveWidth + 32'(x);
    endfunction
endpackage
