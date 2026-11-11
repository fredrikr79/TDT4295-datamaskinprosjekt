module mem_txn_write_enable #(
) (
    input logic reset,
    input logic enable,
    input memory_pkg::state_e current_state,
    input memory_pkg::message_length_t bytes_this_state,
    output memory_pkg::transaction_output_t out
);

  always_comb begin
    out = '{rwds: 'b0, dq: 'h00, next_state: current_state, read_data: 'h00};
    if (reset) begin
      out.next_state = memory_pkg::IDLE;
    end else if (enable) begin
      case (current_state)
        memory_pkg::IDLE: out.next_state = memory_pkg::STARTING_TRANSACTION;
        memory_pkg::STARTING_TRANSACTION: begin
          out.next_state = memory_pkg::SENDING_CMD;
          out.dq = memory_pkg::WRITE_ENABLE;
        end
        memory_pkg::SENDING_CMD:
        if (bytes_this_state == 1) out.next_state = memory_pkg::IDLE;
        else out.dq = memory_pkg::WRITE_ENABLE;
        default: ;
      endcase
    end
  end

endmodule
