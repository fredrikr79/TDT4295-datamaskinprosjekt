module mem_txn_read_any_register (
    input logic reset,
    input logic enable,
    input logic [7:0] dq_in,
    input logic [31:0] address,
    input logic double_latency,
    input memory_pkg::state_e current_state,
    input memory_pkg::message_length_t bytes_this_state,
    output memory_pkg::transaction_output_t out,
    output logic [7:0] read_data
);

  always_comb begin
    if (reset) out = '{rwds: 'b0, dq: 'h00, next_state: memory_pkg::IDLE};
    else if (enable) begin
      case (current_state)
        memory_pkg::IDLE: out.next_state = memory_pkg::STARTING_TRANSACTION;
        memory_pkg::STARTING_TRANSACTION: begin
          out.next_state = memory_pkg::SENDING_CMD;
          out.dq = memory_pkg::READ_ANY_REGISTER;
        end
        memory_pkg::SENDING_CMD: begin
          if (bytes_this_state == 1) begin
            out.next_state = memory_pkg::SENDING_ADDRESS;
            out.dq = address[31-:8];
          end
        end
        memory_pkg::SENDING_ADDRESS: begin
          if (bytes_this_state < 3) out.dq = address[31-8*(bytes_this_state+1)-:8];
          else out.next_state = memory_pkg::WAITING_ON_LATENCY;
        end
        memory_pkg::WAITING_ON_LATENCY: begin
          if (memory_pkg::waiting_is_finished(bytes_this_state, double_latency)) begin
            out.next_state = memory_pkg::READING;
            read_data = dq_in;
          end
        end
        memory_pkg::READING: begin
          read_data = dq_in;
          if (memory_pkg::transaction_is_finished(
                  bytes_this_state, memory_pkg::READ_ANY_REGISTER, 0
              )) begin
            out.next_state = memory_pkg::IDLE;
          end
        end
        default: ;
      endcase
    end
  end

endmodule
