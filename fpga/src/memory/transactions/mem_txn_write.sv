module mem_txn_write (
    input logic reset,
    input logic enable,
    input logic [7:0] dq_in,
    input logic [31:0] address,
    input logic double_latency,
    input memory_pkg::state_e current_state,
    input memory_pkg::message_length_t bytes_this_state,
    write_length,
    output memory_pkg::transaction_output_t out,
    input logic [7:0] write_data
);
  logic [31:0] even_address;
  // [ODD ADDR] Data slots on the bus. The CA address must be even, so an odd address gets a
  // masked byte in front (slot 0). The total is rounded up to whole 16-bit words with a masked
  // byte at the end, so CS# never goes high in the middle of a word.
  //   slot 0 .. lead-1       : masked (lead = address[0])
  //   slot lead .. lead+L-1  : data
  //   slot lead+L .. total-1 : masked (0 or 1 pad byte)
  memory_pkg::message_length_t data_end, total_slots, slot;

  always_comb begin
    even_address = {address[31:1], 1'b0};
    data_end = write_length + memory_pkg::message_length_t'(address[0]);
    total_slots = (data_end + 1'b1) & ~memory_pkg::message_length_t'(1);
    // WRITING drives slot bytes_this_state + 1 (slot 0 went out on the way into WRITING)
    slot = bytes_this_state + 1'b1;
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
            out.rwds = address[0];  // slot 0: masked for an odd address
          end
        end
        memory_pkg::WRITING: begin
          out.dq   = write_data;
          out.rwds = slot >= data_end;  // trailing pad byte is masked
          if (slot >= total_slots) begin
            out.next_state = memory_pkg::IDLE;
          end
        end
        default: ;
      endcase
    end
  end

endmodule
