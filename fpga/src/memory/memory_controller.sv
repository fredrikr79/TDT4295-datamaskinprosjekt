module memory_controller (

    // input from fpga
    input logic clk,
    input logic reset,
    input logic enable,
    input logic start_phase,
    input logic transaction_toggle,

    input memory_pkg::transaction_e next_transaction,
    input memory_pkg::address_t address,
    input memory_pkg::message_length_t length,
    input logic [7:0] write_data,

    // Info to fpga
    output logic busy,
    data_on_cable,
    output logic [7:0] read_data,
    output logic run_clock,

    // Memory pins
    output logic cs_n,
    output logic reset_n,
    input wire [7:0] dq_in,
    output logic [7:0] dq_out,
    output logic dq_oe,
    input wire rwds_in,
    output logic rwds_out,
    output logic rwds_oe
);

  // S70KL/S70KS1283 is dual-die and requires fixed 2X latency.
  wire                               double_latency = 1'b1;
  logic                              current_transaction_toggle;
  memory_pkg::address_t              current_address;
  memory_pkg::message_length_t       current_length;


  assign reset_n = ~reset;

  memory_pkg::state_e current_state, previous_state;
  assign busy = current_state != memory_pkg::IDLE;

  // ---- Read sampling at a fixed, configurable delay after each CK edge ----
  logic [7:0] dq_p;
  always_ff @(posedge clk) dq_p <= dq_in;
  always @(negedge clk) read_data <= memory_pkg::ReadSampleOnNegedge ? dq_in : dq_p;

  // reading_pipe[i] during slot k = (state was READING in slot k-1-i)
  logic [7:0] reading_pipe = '0;
  always @(negedge clk) reading_pipe <= {reading_pipe[6:0], current_state == memory_pkg::READING};

  assign data_on_cable = (current_state == memory_pkg::WRITING) ||
                         reading_pipe[memory_pkg::ReadDataDelay - 1];

  // ---- Insert READ_TAIL after any READING, without touching the txn modules ----
  memory_pkg::transaction_output_t txn_out;
  always_comb begin
    txn_out = find_transaction_output(current_transaction);
    if (current_state == memory_pkg::READ_TAIL) begin
      txn_out = none_out;
      txn_out.next_state = (bytes_this_state == memory_pkg::message_length_t'(memory_pkg::ReadDataDelay - 1))
                           ? memory_pkg::IDLE : memory_pkg::READ_TAIL;
    end else if (current_state == memory_pkg::READING && txn_out.next_state == memory_pkg::IDLE) begin
      txn_out.next_state = memory_pkg::READ_TAIL;
    end
  end

  always_comb begin
    case (current_state)
      memory_pkg::IDLE: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 1;
        run_clock = 0;
      end
      memory_pkg::STARTING_TRANSACTION: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 1;
        run_clock = 0;
      end
      memory_pkg::SENDING_CMD: begin
        dq_oe = 1;
        rwds_oe = 0;
        cs_n = 0;
        run_clock = 1;
      end
      memory_pkg::SENDING_ADDRESS: begin
        dq_oe = 1;
        rwds_oe = 0;
        cs_n = 0;
        run_clock = 1;
      end
      memory_pkg::WAITING_ON_LATENCY: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 0;
        run_clock = 1;
      end
      memory_pkg::READING: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 0;
        run_clock = 1;
      end
      memory_pkg::WRITING: begin
        dq_oe = 1;
        // RWDS masks array writes when high; register writes do not use a mask.
        rwds_oe = current_transaction == memory_pkg::WRITE;
        cs_n = 0;
        run_clock = 1;
      end
      memory_pkg::READ_TAIL: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 0;
        run_clock = 0;
      end
      default: begin
        dq_oe = 0;
        rwds_oe = 0;
        cs_n = 1;
        run_clock = 0;
      end
    endcase
  end

  logic reset_byte_counter;
  assign reset_byte_counter = (current_state == previous_state) ? 0 : 1;
  memory_pkg::message_length_t bytes_this_state;
  mod_n_counter #(
      .N(memory_pkg::MaxMessageLength)
  ) byte_counter (
      .clk(clk),
      .enable(enable),
      .reset(reset_byte_counter | reset),
      .count(bytes_this_state)
  );

  memory_pkg::transaction_e current_transaction;

  function automatic void update_outputs(input memory_pkg::transaction_output_t transaction_out);
    previous_state <= current_state;
    current_state <= transaction_out.next_state;
    dq_out <= transaction_out.dq;
    rwds_out <= transaction_out.rwds;
  endfunction

  always_ff @(posedge clk) begin
    if (reset) begin
      current_transaction <= memory_pkg::NONE;
      current_transaction_toggle <= transaction_toggle;
    // Capture on the clk edge that begins the high start phase. Otherwise the
    // transaction could be cleared before the next allowed state update.
    end else if (enable && !start_phase && current_state == memory_pkg::IDLE) begin
      if (transaction_toggle != current_transaction_toggle) begin
        current_transaction <= next_transaction;
        current_address <= address;
        current_length <= length;
        current_transaction_toggle <= transaction_toggle;
      end else begin
        current_transaction <= memory_pkg::NONE;
      end
    end
  end
  always @(negedge clk) begin
    if (reset) begin
      update_outputs(find_transaction_output(memory_pkg::NONE));
    end else if (enable &&
                 (current_state != memory_pkg::IDLE &&
                  current_state != memory_pkg::STARTING_TRANSACTION || start_phase)) begin
      update_outputs(txn_out);
    end
  end

  memory_pkg::transaction_output_t none_out = '{
      rwds: 'b0,
      dq: 'h00,
      next_state: memory_pkg::IDLE,
      read_data: 'h00
  };
  memory_pkg::transaction_output_t reset_enable_out;
  mem_txn_reset_enable reset_enable_transaction (
      .reset(reset),
      .enable(enable),
      .rwds_in(rwds_in),
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(reset_enable_out)
  );
  memory_pkg::transaction_output_t reset_txn_out;
  mem_txn_reset reset_transaction (
      .reset(reset),
      .enable(enable),
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(reset_txn_out)
  );
  memory_pkg::transaction_output_t deep_power_down_txn_out;
  mem_txn_deep_power_down deep_power_down_transaction (
      .reset(reset),
      .enable(enable),
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(deep_power_down_txn_out)
  );
  memory_pkg::transaction_output_t deep_power_up_txn_out;
  mem_txn_deep_power_up deep_power_up_transaction (
      .reset(reset),
      .enable(enable),
      .current_state(current_state),
      .out(deep_power_up_txn_out)
  );
  memory_pkg::transaction_output_t write_enable_txn_out;
  mem_txn_write_enable write_enable_transaction (
      .reset(reset),
      .enable(enable),
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(write_enable_txn_out)
  );
  memory_pkg::transaction_output_t write_disable_txn_out;
  mem_txn_write_disable write_disable_transaction (
      .reset(reset),
      .enable(enable),
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(write_disable_txn_out)
  );
  memory_pkg::transaction_output_t write_any_register_txn_out;
  mem_txn_write_any_register write_any_register_transaction (
      .reset(reset),
      .enable(enable),
      .address(current_address),
      .write_data,
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(write_any_register_txn_out)
  );
  memory_pkg::transaction_output_t read_any_register_txn_out;
  mem_txn_read_any_register read_any_register_transaction (
      .reset(reset),
      .enable(enable),
      .dq_in(dq_in),
      .address(current_address),
      .double_latency,
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(read_any_register_txn_out)
  );
  memory_pkg::transaction_output_t read_id_txn_out;
  mem_txn_read_id read_id_transaction (
      .reset(reset),
      .enable(enable),
      .dq_in(dq_in),
      .double_latency,
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(read_id_txn_out)
  );
  memory_pkg::transaction_output_t read_txn_out;
  mem_txn_read read_transaction (
      .reset(reset),
      .enable(enable),
      .dq_in(dq_in),
      .address(current_address),
      .read_length(current_length),
      .double_latency,
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(read_txn_out)
  );
  memory_pkg::transaction_output_t write_txn_out;
  mem_txn_write write_transaction (
      .reset(reset),
      .enable(enable),
      .dq_in(dq_in),
      .address(current_address),
      .write_length(current_length),
      .double_latency,
      .write_data,
      .current_state(current_state),
      .bytes_this_state(bytes_this_state),
      .out(write_txn_out)
  );

  function automatic memory_pkg::transaction_output_t find_transaction_output(
      input memory_pkg::transaction_e transaction);
    case (transaction)
      memory_pkg::RESET_ENABLE: return reset_enable_out;
      memory_pkg::RESET: return reset_txn_out;
      memory_pkg::DEEP_POWER_DOWN: return deep_power_down_txn_out;
      memory_pkg::DEEP_POWER_UP: return deep_power_up_txn_out;
      memory_pkg::WRITE_ENABLE: return write_enable_txn_out;
      memory_pkg::WRITE_DISABLE: return write_disable_txn_out;
      memory_pkg::WRITE_ANY_REGISTER: return write_any_register_txn_out;
      memory_pkg::READ_ANY_REGISTER: return read_any_register_txn_out;
      memory_pkg::READ_ID: return read_id_txn_out;
      memory_pkg::READ: return read_txn_out;
      memory_pkg::WRITE: return write_txn_out;
      default: return none_out;
    endcase
  endfunction


endmodule
