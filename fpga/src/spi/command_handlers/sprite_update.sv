module sprite_update #(

)(
    input wire clk, ck_rst, send_sprite_update,
    input logic in_empty,
    input logic [7:0] in_fifo,
    input logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
    output logic sprite_update_done,
    sprite_update_r_en,
    sprite_req_valid,
    error_valid,
    output logic [7:0] error_code,
    output hud_pkg::sprite_inst_entry_t sprite_req_body
);

import hud_pkg::*;
import spi_pck::*;

typedef enum bit [3:0] {
    START,
    GET_ID_TOP,
    GET_ID_BOTTOM,
    GET_MEM_ID_TOP,
    GET_MEM_ID_BOTTOM,
    GET_ACTIVE,
    GET_X_TOP,
    GET_X_BOTTOM,
    GET_Y_TOP,
    GET_Y_BOTTOM,
    EXECUTE,
    DONE
} sprite_update_states_e;

sprite_update_states_e state = START;

logic param_active;
hud_pkg::sprite_lut_id param_id;
hud_pkg::sprite_mem_id param_mem_id;
logic [15:0] param_x, param_y;

always_ff @(posedge clk) begin
    sprite_update_r_en <= 1'b0;
    sprite_update_done <= 1'b0;
    sprite_req_valid   <= 1'b0;

    if(ck_rst) begin
        // Params will be overwritten during next execution
        state <= START;
        sprite_update_done <= 1'b0;
    end else begin
        if(send_sprite_update) begin
            case (state)
                START: begin
                    sprite_update_r_en <= 1'b1;
                    state <= GET_ID_TOP;
                end
                GET_ID_TOP: begin
                    if($bits(hud_pkg::sprite_lut_id) > 8) begin
                        sprite_update_r_en <= 1'b1;
                        param_id[$bits(hud_pkg::sprite_lut_id)-1:8] <= in_fifo[$bits(hud_pkg::sprite_lut_id)-9:0];
                    end
                    state <= GET_ID_BOTTOM;
                end
                GET_ID_BOTTOM: begin
                    sprite_update_r_en <= 1'b1;
                    param_id[7:0] <= in_fifo;
                    state <= GET_MEM_ID_TOP;
                end
                GET_MEM_ID_TOP: begin
                    if($bits(hud_pkg::sprite_mem_id) > 8) begin
                        sprite_update_r_en <= 1'b1;
                        param_mem_id[$bits(hud_pkg::sprite_mem_id)-1:8] <= in_fifo[$bits(hud_pkg::sprite_mem_id)-9:0];
                    end
                    state <= GET_MEM_ID_BOTTOM;
                end
                GET_MEM_ID_BOTTOM: begin
                    sprite_update_r_en <= 1'b1;
                    param_mem_id[7:0] <= in_fifo;
                    state <= GET_ACTIVE;
                end
                GET_ACTIVE: begin
                    sprite_update_r_en <= 1'b1;
                    param_active <= in_fifo[0];
                    state <= GET_X_TOP;
                end
                GET_X_TOP: begin
                    sprite_update_r_en <= 1'b1;
                    param_x[15:8] <= in_fifo;
                    state <= GET_X_BOTTOM;
                end
                GET_X_BOTTOM: begin
                    sprite_update_r_en <= 1'b1;
                    param_x[7:0] <= in_fifo;
                    state <= GET_Y_TOP;
                end
                GET_Y_TOP: begin
                    sprite_update_r_en <= 1'b1;
                    param_y[15:8] <= in_fifo;
                    state <= GET_Y_BOTTOM;
                end
                GET_Y_BOTTOM: begin
                    param_y[7:0] <= in_fifo;
                    state <= EXECUTE;
                    if(in_empty) begin
                        error_valid <= 1'b1;
                        error_code <= MISSING_PARAMETER_ERROR;
                    end
                end
                EXECUTE: begin
                    if(!sprite_id_bitmap[param_id]) begin
                        error_valid <= 1'b1;
                        error_code <= UNKNOWN_IDENTIFIER_ERROR;
                    end else begin
                        sprite_req_body.id <= param_id;
                        sprite_req_body.mem_id <= param_mem_id;
                        sprite_req_body.active <= param_active;
                        sprite_req_body.x <= param_x;
                        sprite_req_body.y <= param_y;
                        sprite_req_valid <= 1'b1;
                        sprite_update_done <= 1'b1;
                        state <= DONE;
                    end
                end
                DONE: begin
                    // Simply wait one cycle for done signal to be processed.
                    state <= START;
                end
                default:;
            endcase
            end
    end
end


endmodule
