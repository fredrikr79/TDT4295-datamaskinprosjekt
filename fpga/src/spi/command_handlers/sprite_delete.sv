module sprite_delete #(

)(
    input wire clk, ck_rst, send_sprite_delete,
    input logic in_empty,
    input logic [7:0] in_fifo,
    input logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap,
    output logic sprite_delete_done,
    sprite_delete_r_en,
    sprite_req_valid,
    error_valid,
    output logic [7:0] error_code,
    output hud_pkg::sprite_inst_entry_t sprite_req_body
);

import hud_pkg::*;
import spi_pck::*;


typedef enum bit [2:0] {
    START,
    GET_ID_TOP,
    GET_ID_BOTTOM,
    EXECUTE,
    DONE
} sprite_delete_states_e;

sprite_delete_states_e state = START;

hud_pkg::sprite_lut_id param_id;

always_ff @(posedge clk) begin
    sprite_delete_r_en <= 1'b0;
    sprite_delete_done <= 1'b0;
    sprite_req_valid   <= 1'b0;

    if (ck_rst) begin
        state <= START;
    end else if(send_sprite_delete) begin
        case (state)
            START: begin
                sprite_delete_r_en <= 1'b1;
                state <= GET_ID_TOP;
            end
            GET_ID_TOP: begin
                if($bits(hud_pkg::sprite_lut_id) > 8) begin
                    sprite_delete_r_en <= 1'b1;
                    param_id[$bits(hud_pkg::sprite_lut_id)-1:8] <= in_fifo[$bits(hud_pkg::sprite_lut_id)-9:0];
                end
                state <= GET_ID_BOTTOM;
            end
            GET_ID_BOTTOM: begin
                sprite_delete_r_en <= 1'b1;
                param_id[7:0] <= in_fifo;
                state <= EXECUTE;
            end
            EXECUTE: begin
                if(!sprite_id_bitmap[param_id]) begin
                    error_valid <= 1'b1;
                    error_code <= UNKNOWN_IDENTIFIER_ERROR;
                end else begin
                    sprite_req_body.id <= param_id;
                    sprite_req_valid <= 1'b1;
                    sprite_delete_done <= 1'b1;
                    state <= DONE;
                end
            end
            DONE: begin
                // Simply wait one cycle for done signal to be processed.
                state <= START;
            end
            default: ;
        endcase
    end
end


endmodule
