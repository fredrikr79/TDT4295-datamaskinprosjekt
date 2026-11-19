module mem_txn_write (
    input logic reset,
    input logic enable,
    input logic [31:0] address,
    input logic double_latency,
    input memory_pkg::state_e current_state,
    input memory_pkg::message_length_t bytes_this_state,
    write_length,
    output memory_pkg::transaction_output_t out,
    input logic [7:0] write_data
);
  logic [31:0] even_address;
  memory_pkg::message_length_t data_end, total_data_slots, data_slot;

  always_comb begin
    even_address = {address[31:1], 1'b0};
    data_end = write_length + memory_pkg::message_length_t'(address[0]);
    total_data_slots = data_end + memory_pkg::message_length_t'(data_end[0]);
    data_slot = bytes_this_state + 1'b1;
    out = '{rwds: 'b0, dq: 'h00, next_state: current_state, read_data: 'h00};
    if (reset) out.next_state = memory_pkg::IDLE;
    else if (enable) begin
      case (current_state)
        memory_pkg::IDLE: out.next_state = memory_pkg::STARTING_TRANSACTION;
        memory_pkg::STARTING_TRANSACTION: begin
          out.next_state = memory_pkg::SENDING_CMD;
          out.dq = memory_pkg::WRITE;
        end
        memory_pkg::SENDING_CMD: begin
          if (bytes_this_state == 1) begin
            out.next_state = memory_pkg::SENDING_ADDRESS;
            out.dq = even_address[31-:8];
          end else out.dq = memory_pkg::WRITE;
        end
        memory_pkg::SENDING_ADDRESS: begin
          if (bytes_this_state < 3) out.dq = even_address[31-8*(bytes_this_state+1)-:8];
          else out.next_state = memory_pkg::WAITING_ON_LATENCY;
        end
        memory_pkg::WAITING_ON_LATENCY: begin
          if (memory_pkg::waiting_is_finished(bytes_this_state, double_latency)) begin
            out.next_state = memory_pkg::WRITING;
            out.dq = write_data;
            out.rwds = address[0];
          end
        end
        memory_pkg::WRITING: begin
          out.dq   = write_data;
          out.rwds = data_slot >= data_end;
          if (data_slot >= total_data_slots) begin
            out.next_state = memory_pkg::IDLE;
          end
        end
        default: ;
      endcase
    end
  end

endmodule
