module mem_txn_write_any_register #(
) (
    input logic reset,
    input logic enable,
    input logic [31:0] address,
    input logic [7:0] write_data,
    input memory_pkg::state_e current_state,
    input memory_pkg::message_length_t bytes_this_state,
    output memory_pkg::transaction_output_t out
);

  always_comb begin
    if (reset) out = '{rwds: 'b0, dq: 'h00, next_state: memory_pkg::IDLE};
    else if (enable) begin
      case (current_state)
        memory_pkg::IDLE: out.next_state = memory_pkg::STARTING_TRANSACTION;
        memory_pkg::STARTING_TRANSACTION: begin
          out.next_state = memory_pkg::SENDING_CMD;
          out.dq = memory_pkg::WRITE_ANY_REGISTER;
        end
        memory_pkg::SENDING_CMD: begin
          if (bytes_this_state == 1) begin
            out.next_state = memory_pkg::SENDING_ADDRESS;
            out.dq = address[31-:8];
          end
        end
        memory_pkg::SENDING_ADDRESS: begin
          if (bytes_this_state < 3) out.dq = address[31-8*(bytes_this_state+1)-:8];
          else begin
            out.next_state = memory_pkg::WRITING;
            out.dq = write_data;
            out.rwds = 1;
          end
        end
        memory_pkg::WRITING: begin
          out.dq = write_data;
          if (bytes_this_state == 1) out.next_state = memory_pkg::IDLE;
        end
        default: ;
      endcase
    end
  end

endmodule
