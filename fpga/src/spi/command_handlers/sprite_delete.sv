module sprite_delete #(

)(
    input wire clk, ck_rst, send_sprite_delete,
    input logic [7:0] in_fifo,
    output logic sprite_delete_done,
    sprite_delete_r_en,
    sprite_req_valid,
    output hud_pkg::sprite_inst_entry_t sprite_req_body
);

import hud_pkg::*;


typedef enum bit [1:0] {
    START,
    GET_ID,
    EXECUTE,
    DONE
} sprite_delete_states_e;

sprite_delete_states_e state = START;

logic [7:0] param_id;

always_ff @(posedge clk) begin
    sprite_delete_r_en <= 1'b0;
    sprite_delete_done <= 1'b0;
    sprite_req_valid   <= 1'b0;

    if(send_sprite_delete) begin
        case (state)
            START: begin
                sprite_delete_r_en <= 1'b1;
                state <= GET_ID;
            end
            GET_ID: begin
                param_id <= in_fifo;
                state <= EXECUTE;
            end
            EXECUTE: begin
                sprite_req_body.id <= param_id;
                sprite_req_valid <= 1'b1;
                sprite_delete_done <= 1'b1;
                state <= DONE;
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
