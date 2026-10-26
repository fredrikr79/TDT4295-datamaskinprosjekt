module memory_initializer (
    input clk,
    reset,
    enable,
    controller_busy,
    controller_data_on_wire,
    output logic finished,
    controller_toggle,
    output memory_pkg::transaction_e next_transaction,
    output memory_pkg::address_t next_address,
    output memory_pkg::message_length_t next_length,
    output logic [7:0] write
);

  localparam int NInitTransactions = 1;
  localparam int TransactionCount  = NInitTransactions > 1 ? $clog2(NInitTransactions) : 0;
  typedef struct {
    memory_pkg::transaction_e transaction;
    memory_pkg::address_t address;
    memory_pkg::message_length_t length;
    logic [7:0] write_data[memory_pkg::MaxMessageLength];
  } memory_meta_data_t;

  localparam memory_meta_data_t InitTransactions[NInitTransactions] = '{
      '{
          transaction: memory_pkg::WRITE_ENABLE,
          address: 31'b0,
          length: 0,
          write_data: '{default: 'h00}
      }
  };
  logic [TransactionCount-1:0] i_current_transactions;
  memory_pkg::message_length_t current_byte_written;

  always_ff @(posedge clk) begin
    if (reset) begin
      finished <= 0;
      next_transaction <= memory_pkg::NONE;
      next_length <= 0;
      i_current_transactions <= 0;
    end else if (enable) begin
      if (i_current_transactions >= NInitTransactions) begin
        finished <= 1;
      end else if (!controller_busy) begin
        next_transaction <= InitTransactions[i_current_transactions].transaction;
        next_address <= InitTransactions[i_current_transactions].address;
        next_length <= InitTransactions[i_current_transactions].length;
        write <= InitTransactions[i_current_transactions].write_data[0];
        controller_toggle <= ~controller_toggle;
        i_current_transactions <= i_current_transactions + 1;
        current_byte_written <= 1;
      end else if (controller_data_on_wire) begin
        write <= InitTransactions[i_current_transactions].write_data[current_byte_written];
        current_byte_written <= current_byte_written + 1;
      end
    end
  end
endmodule
