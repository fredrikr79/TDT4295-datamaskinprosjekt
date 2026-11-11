module mem_txn_deep_power_up #(
) (
    input logic reset,
    input logic enable,
    input memory_pkg::state_e current_state,
    output memory_pkg::transaction_output_t out
);

  always_comb begin
    out = '{rwds: 'b0, dq: 'h00, next_state: memory_pkg::IDLE, read_data: 'h00};
    if (!reset && enable) begin
      case (current_state)
        memory_pkg::IDLE: out.next_state = memory_pkg::SENDING_CMD;
        memory_pkg::SENDING_CMD: out.next_state = memory_pkg::IDLE;
        default: ;
      endcase
    end
  end

endmodule
