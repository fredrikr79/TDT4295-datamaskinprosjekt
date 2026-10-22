module sprite_write #(

)(
    input wire clk, ck_rst, send_sprite_write, sprite_id_found,
    input logic [7:0] in_fifo,
    input logic [$clog2(hud_pkg::SpriteLutDepth)-1:0] sprite_available_id,
    output logic sprite_write_r_en,
        sprite_write_w_en,
        sprite_write_done,
        sprite_req_valid,
        sprite_id_found,
        error_code,
    output logic [7:0] sprite_write_fifo,
        error_code,
    output hud_pkg::sprite_inst_entry_t sprite_req_body
);

import hud_pkg::*;

typedef enum bit [3:0] {
    START,
    GET_MEM_ID,
    GET_ACTIVE,
    GET_X_TOP,
    GET_X_BOTTOM,
    GET_Y_TOP,
    GET_Y_BOTTOM,
    GET_WIDTH_TOP,
    GET_WIDTH_BOTTOM,
    GET_HEIGTH_TOP,
    GET_HEIGTH_BOTTOM,
    EXECUTE,
    DONE
} sprite_write_states_e;

sprite_write_states_e state = START;

logic param_active;
logic [7:0]  param_id, param_mem_id;
logic [15:0] param_x, param_y, param_w, param_h;
logic [31:0] param_mem_addr;

always_ff @(posedge clk) begin
    sprite_write_r_en <= 1'b0;
    sprite_write_w_en <= 1'b0;
    sprite_write_done <= 1'b0;
    sprite_req_valid  <= 1'b0;
    error_valid       <= 1'b0;

    if (ck_rst) begin
        sprite_write_fifo <= 8'd0;
        state <= START;
    end else begin
       if(send_sprite_write) begin
        case (state)
            START: begin
                // Await available ID
                if(sprite_id_found) begin
                    sprite_write_r_en <= 1'b1;
                    param_id <= sprite_available_id;
                    state <= GET_MEM_ID;
                end else begin
                    error_valid <= 1'b1;
                    error_code <= RESOURCE_EXHAUSTED_ERROR;
                end
            end
            GET_MEM_ID: begin
                sprite_write_r_en <= 1'b1;
                param_mem_id <= in_fifo;
                state <= GET_ACTIVE;
            end
            GET_ACTIVE: begin
                sprite_write_r_en <= 1'b1;
                param_active <= in_fifo[0];
                state <= GET_X_TOP;
            end
            GET_X_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_x[15:8] <= in_fifo;
                state <= GET_X_BOTTOM;
            end
            GET_X_BOTTOM: begin
                sprite_write_r_en <= 1'b1;
                param_x[7:0] <= in_fifo;
                state <= GET_Y_TOP;
            end
            GET_Y_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_y[15:8] <= in_fifo;
                state <= GET_Y_BOTTOM;
            end
            GET_Y_BOTTOM: begin
                sprite_write_r_en <= 1'b1;
                param_y[7:0] <= in_fifo;
                state <= GET_WIDTH_TOP;
            end
            GET_WIDTH_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_w[15:8] <= in_fifo;
                state <= GET_WIDTH_BOTTOM;
            end
            GET_WIDTH_BOTTOM: begin
                sprite_write_r_en <= 1'b1;
                param_w[7:0] <= in_fifo;
                state <= GET_HEIGTH_TOP;
            end
            GET_HEIGTH_TOP: begin
                sprite_write_r_en <= 1'b1;
                param_h[15:8] <= in_fifo;
                state <= GET_HEIGTH_BOTTOM;
            end
            GET_HEIGTH_BOTTOM: begin
                param_h[7:0] <= in_fifo;
                state <= EXECUTE;
                if(in_empty) begin
                    error_valid <= 1'b1;
                    error_code <= MISSING_PARAMETER_ERROR;
                end
            end
            EXECUTE: begin
                sprite_req_body <= {
                    1'b1,
                    param_id,
                    param_mem_id,
                    param_x,
                    param_y,
                    param_w,
                    param_h
                };
                sprite_req_valid <= 1'b1;
                sprite_write_w_en <= 1'b1;
                sprite_write_fifo <= param_id;
                sprite_write_done <= 1'b1;
                state <= DONE;
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
